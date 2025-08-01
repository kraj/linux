// SPDX-License-Identifier: GPL-2.0+
/*
 *  Universal/legacy driver for 8250/16550-type serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King.
 *
 *  Supports:
 *	      early_serial_setup() ports
 *	      userspace-configurable "phantom" ports
 *	      serial8250_register_8250_port() ports
 */

#include <linux/acpi.h>
#include <linux/hashtable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/tty.h>
#include <linux/ratelimit.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/nmi.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <asm/irq.h>

#include "8250.h"

#define PASS_LIMIT	512

struct irq_info {
	struct			hlist_node node;
	int			irq;
	spinlock_t		lock;	/* Protects list not the hash */
	struct list_head	*head;
};

#define IRQ_HASH_BITS		5	/* Can be adjusted later */
static DEFINE_HASHTABLE(irq_lists, IRQ_HASH_BITS);
static DEFINE_MUTEX(hash_mutex);	/* Used to walk the hash */

/*
 * This is the serial driver's interrupt routine.
 *
 * Arjan thinks the old way was overly complex, so it got simplified.
 * Alan disagrees, saying that need the complexity to handle the weird
 * nature of ISA shared interrupts.  (This is a special exception.)
 *
 * In order to handle ISA shared interrupts properly, we need to check
 * that all ports have been serviced, and therefore the ISA interrupt
 * line has been de-asserted.
 *
 * This means we need to loop through all ports. checking that they
 * don't have an interrupt pending.
 */
static irqreturn_t serial8250_interrupt(int irq, void *dev_id)
{
	struct irq_info *i = dev_id;
	struct list_head *l, *end = NULL;
	int pass_counter = 0, handled = 0;

	spin_lock(&i->lock);

	l = i->head;
	do {
		struct uart_8250_port *up = list_entry(l, struct uart_8250_port, list);
		struct uart_port *port = &up->port;

		if (port->handle_irq(port)) {
			handled = 1;
			end = NULL;
		} else if (end == NULL)
			end = l;

		l = l->next;

		if (l == i->head && pass_counter++ > PASS_LIMIT)
			break;
	} while (l != end);

	spin_unlock(&i->lock);

	return IRQ_RETVAL(handled);
}

/*
 * To support ISA shared interrupts, we need to have one interrupt
 * handler that ensures that the IRQ line has been deasserted
 * before returning.  Failing to do this will result in the IRQ
 * line being stuck active, and, since ISA irqs are edge triggered,
 * no more IRQs will be seen.
 */
static void serial_do_unlink(struct irq_info *i, struct uart_8250_port *up)
{
	spin_lock_irq(&i->lock);

	if (!list_empty(i->head)) {
		if (i->head == &up->list)
			i->head = i->head->next;
		list_del(&up->list);
	} else {
		BUG_ON(i->head != &up->list);
		i->head = NULL;
	}
	spin_unlock_irq(&i->lock);
	/* List empty so throw away the hash node */
	if (i->head == NULL) {
		hlist_del(&i->node);
		kfree(i);
	}
}

/*
 * Either:
 * - find the corresponding info in the hashtable and return it, or
 * - allocate a new one, add it to the hashtable and return it.
 */
static struct irq_info *serial_get_or_create_irq_info(const struct uart_8250_port *up)
{
	struct irq_info *i;

	mutex_lock(&hash_mutex);

	hash_for_each_possible(irq_lists, i, node, up->port.irq)
		if (i->irq == up->port.irq)
			goto unlock;

	i = kzalloc(sizeof(*i), GFP_KERNEL);
	if (i == NULL) {
		i = ERR_PTR(-ENOMEM);
		goto unlock;
	}
	spin_lock_init(&i->lock);
	i->irq = up->port.irq;
	hash_add(irq_lists, &i->node, i->irq);
unlock:
	mutex_unlock(&hash_mutex);

	return i;
}

static int serial_link_irq_chain(struct uart_8250_port *up)
{
	struct irq_info *i;
	int ret;

	i = serial_get_or_create_irq_info(up);
	if (IS_ERR(i))
		return PTR_ERR(i);

	spin_lock_irq(&i->lock);

	if (i->head) {
		list_add(&up->list, i->head);
		spin_unlock_irq(&i->lock);

		ret = 0;
	} else {
		INIT_LIST_HEAD(&up->list);
		i->head = &up->list;
		spin_unlock_irq(&i->lock);
		ret = request_irq(up->port.irq, serial8250_interrupt,
				  up->port.irqflags, up->port.name, i);
		if (ret < 0)
			serial_do_unlink(i, up);
	}

	return ret;
}

static void serial_unlink_irq_chain(struct uart_8250_port *up)
{
	struct irq_info *i;

	mutex_lock(&hash_mutex);

	hash_for_each_possible(irq_lists, i, node, up->port.irq)
		if (i->irq == up->port.irq)
			break;

	BUG_ON(i == NULL);
	BUG_ON(i->head == NULL);

	if (list_empty(i->head))
		free_irq(up->port.irq, i);

	serial_do_unlink(i, up);
	mutex_unlock(&hash_mutex);
}

/*
 * This function is used to handle ports that do not have an
 * interrupt.  This doesn't work very well for 16450's, but gives
 * barely passable results for a 16550A.  (Although at the expense
 * of much CPU overhead).
 */
static void serial8250_timeout(struct timer_list *t)
{
	struct uart_8250_port *up = timer_container_of(up, t, timer);

	up->port.handle_irq(&up->port);
	mod_timer(&up->timer, jiffies + uart_poll_timeout(&up->port));
}

static void serial8250_backup_timeout(struct timer_list *t)
{
	struct uart_8250_port *up = timer_container_of(up, t, timer);
	unsigned int iir, ier = 0, lsr;
	unsigned long flags;

	uart_port_lock_irqsave(&up->port, &flags);

	/*
	 * Must disable interrupts or else we risk racing with the interrupt
	 * based handler.
	 */
	if (up->port.irq) {
		ier = serial_in(up, UART_IER);
		serial_out(up, UART_IER, 0);
	}

	iir = serial_in(up, UART_IIR);

	/*
	 * This should be a safe test for anyone who doesn't trust the
	 * IIR bits on their UART, but it's specifically designed for
	 * the "Diva" UART used on the management processor on many HP
	 * ia64 and parisc boxes.
	 */
	lsr = serial_lsr_in(up);
	if ((iir & UART_IIR_NO_INT) && (up->ier & UART_IER_THRI) &&
	    (!kfifo_is_empty(&up->port.state->port.xmit_fifo) ||
	     up->port.x_char) &&
	    (lsr & UART_LSR_THRE)) {
		iir &= ~(UART_IIR_ID | UART_IIR_NO_INT);
		iir |= UART_IIR_THRI;
	}

	if (!(iir & UART_IIR_NO_INT))
		serial8250_tx_chars(up);

	if (up->port.irq)
		serial_out(up, UART_IER, ier);

	uart_port_unlock_irqrestore(&up->port, flags);

	/* Standard timer interval plus 0.2s to keep the port running */
	mod_timer(&up->timer,
		jiffies + uart_poll_timeout(&up->port) + HZ / 5);
}

static void univ8250_setup_timer(struct uart_8250_port *up)
{
	struct uart_port *port = &up->port;

	/*
	 * The above check will only give an accurate result the first time
	 * the port is opened so this value needs to be preserved.
	 */
	if (up->bugs & UART_BUG_THRE) {
		pr_debug("%s - using backup timer\n", port->name);

		up->timer.function = serial8250_backup_timeout;
		mod_timer(&up->timer, jiffies +
			  uart_poll_timeout(port) + HZ / 5);
	}

	/*
	 * If the "interrupt" for this port doesn't correspond with any
	 * hardware interrupt, we use a timer-based system.  The original
	 * driver used to do this with IRQ0.
	 */
	if (!port->irq)
		mod_timer(&up->timer, jiffies + uart_poll_timeout(port));
}

static int univ8250_setup_irq(struct uart_8250_port *up)
{
	struct uart_port *port = &up->port;

	if (port->irq)
		return serial_link_irq_chain(up);

	return 0;
}

static void univ8250_release_irq(struct uart_8250_port *up)
{
	struct uart_port *port = &up->port;

	timer_delete_sync(&up->timer);
	up->timer.function = serial8250_timeout;
	if (port->irq)
		serial_unlink_irq_chain(up);
}

const struct uart_ops *univ8250_port_base_ops = NULL;
struct uart_ops univ8250_port_ops;

static const struct uart_8250_ops univ8250_driver_ops = {
	.setup_irq	= univ8250_setup_irq,
	.release_irq	= univ8250_release_irq,
	.setup_timer	= univ8250_setup_timer,
};

static struct uart_8250_port serial8250_ports[UART_NR];

/**
 * serial8250_get_port - retrieve struct uart_8250_port
 * @line: serial line number
 *
 * This function retrieves struct uart_8250_port for the specific line.
 * This struct *must* *not* be used to perform a 8250 or serial core operation
 * which is not accessible otherwise. Its only purpose is to make the struct
 * accessible to the runtime-pm callbacks for context suspend/restore.
 * The lock assumption made here is none because runtime-pm suspend/resume
 * callbacks should not be invoked if there is any operation performed on the
 * port.
 */
struct uart_8250_port *serial8250_get_port(int line)
{
	return &serial8250_ports[line];
}
EXPORT_SYMBOL_GPL(serial8250_get_port);

static inline void serial8250_apply_quirks(struct uart_8250_port *up)
{
	up->port.quirks |= skip_txen_test ? UPQ_NO_TXEN_TEST : 0;
}

struct uart_8250_port *serial8250_setup_port(int index)
{
	struct uart_8250_port *up;

	if (index >= UART_NR)
		return NULL;

	up = &serial8250_ports[index];
	up->port.line = index;
	up->port.port_id = index;

	serial8250_init_port(up);
	if (!univ8250_port_base_ops)
		univ8250_port_base_ops = up->port.ops;
	up->port.ops = &univ8250_port_ops;

	timer_setup(&up->timer, serial8250_timeout, 0);

	up->ops = &univ8250_driver_ops;

	serial8250_set_defaults(up);

	return up;
}

void __init serial8250_register_ports(struct uart_driver *drv, struct device *dev)
{
	int i;

	for (i = 0; i < nr_uarts; i++) {
		struct uart_8250_port *up = &serial8250_ports[i];

		if (up->port.type == PORT_8250_CIR)
			continue;

		if (up->port.dev)
			continue;

		up->port.dev = dev;

		if (uart_console_registered(&up->port))
			pm_runtime_get_sync(up->port.dev);

		serial8250_apply_quirks(up);
		uart_add_one_port(drv, &up->port);
	}
}

#ifdef CONFIG_SERIAL_8250_CONSOLE

static void univ8250_console_write(struct console *co, const char *s,
				   unsigned int count)
{
	struct uart_8250_port *up = &serial8250_ports[co->index];

	serial8250_console_write(up, s, count);
}

static int univ8250_console_setup(struct console *co, char *options)
{
	struct uart_8250_port *up;
	struct uart_port *port;
	int retval, i;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index < 0 || co->index >= UART_NR)
		co->index = 0;

	/*
	 * If the console is past the initial isa ports, init more ports up to
	 * co->index as needed and increment nr_uarts accordingly.
	 */
	for (i = nr_uarts; i <= co->index; i++) {
		up = serial8250_setup_port(i);
		if (!up)
			return -ENODEV;
		nr_uarts++;
	}

	port = &serial8250_ports[co->index].port;
	/* link port to console */
	uart_port_set_cons(port, co);

	retval = serial8250_console_setup(port, options, false);
	if (retval != 0)
		uart_port_set_cons(port, NULL);
	return retval;
}

static int univ8250_console_exit(struct console *co)
{
	struct uart_port *port;

	port = &serial8250_ports[co->index].port;
	return serial8250_console_exit(port);
}

/**
 *	univ8250_console_match - non-standard console matching
 *	@co:	  registering console
 *	@name:	  name from console command line
 *	@idx:	  index from console command line
 *	@options: ptr to option string from console command line
 *
 *	Only attempts to match console command lines of the form:
 *	    console=uart[8250],io|mmio|mmio16|mmio32,<addr>[,<options>]
 *	    console=uart[8250],0x<addr>[,<options>]
 *	This form is used to register an initial earlycon boot console and
 *	replace it with the serial8250_console at 8250 driver init.
 *
 *	Performs console setup for a match (as required by interface)
 *	If no <options> are specified, then assume the h/w is already setup.
 *
 *	Returns 0 if console matches; otherwise non-zero to use default matching
 */
static int univ8250_console_match(struct console *co, char *name, int idx,
				  char *options)
{
	char match[] = "uart";	/* 8250-specific earlycon name */
	enum uart_iotype iotype;
	resource_size_t addr;
	int i;

	if (strncmp(name, match, 4) != 0)
		return -ENODEV;

	if (uart_parse_earlycon(options, &iotype, &addr, &options))
		return -ENODEV;

	/* try to match the port specified on the command line */
	for (i = 0; i < nr_uarts; i++) {
		struct uart_port *port = &serial8250_ports[i].port;

		if (port->iotype != iotype)
			continue;
		if ((iotype == UPIO_MEM || iotype == UPIO_MEM16 ||
		     iotype == UPIO_MEM32 || iotype == UPIO_MEM32BE)
		    && (port->mapbase != addr))
			continue;
		if (iotype == UPIO_PORT && port->iobase != addr)
			continue;

		co->index = i;
		uart_port_set_cons(port, co);
		return serial8250_console_setup(port, options, true);
	}

	return -ENODEV;
}

static struct console univ8250_console = {
	.name		= "ttyS",
	.write		= univ8250_console_write,
	.device		= uart_console_device,
	.setup		= univ8250_console_setup,
	.exit		= univ8250_console_exit,
	.match		= univ8250_console_match,
	.flags		= CON_PRINTBUFFER | CON_ANYTIME,
	.index		= -1,
	.data		= &serial8250_reg,
};

static int __init univ8250_console_init(void)
{
	if (nr_uarts == 0)
		return -ENODEV;

	serial8250_isa_init_ports();
	register_console(&univ8250_console);
	return 0;
}
console_initcall(univ8250_console_init);

#define SERIAL8250_CONSOLE	(&univ8250_console)
#else
#define SERIAL8250_CONSOLE	NULL
#endif

struct uart_driver serial8250_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.cons			= SERIAL8250_CONSOLE,
};

/*
 * early_serial_setup - early registration for 8250 ports
 *
 * Setup an 8250 port structure prior to console initialisation.  Use
 * after console initialisation will cause undefined behaviour.
 */
int __init early_serial_setup(struct uart_port *port)
{
	struct uart_port *p;

	if (port->line >= ARRAY_SIZE(serial8250_ports) || nr_uarts == 0)
		return -ENODEV;

	serial8250_isa_init_ports();
	p = &serial8250_ports[port->line].port;
	p->iobase       = port->iobase;
	p->membase      = port->membase;
	p->irq          = port->irq;
	p->irqflags     = port->irqflags;
	p->uartclk      = port->uartclk;
	p->fifosize     = port->fifosize;
	p->regshift     = port->regshift;
	p->iotype       = port->iotype;
	p->flags        = port->flags;
	p->mapbase      = port->mapbase;
	p->mapsize      = port->mapsize;
	p->private_data = port->private_data;
	p->type		= port->type;
	p->line		= port->line;

	serial8250_set_defaults(up_to_u8250p(p));

	if (port->serial_in)
		p->serial_in = port->serial_in;
	if (port->serial_out)
		p->serial_out = port->serial_out;
	if (port->handle_irq)
		p->handle_irq = port->handle_irq;

	return 0;
}

/**
 *	serial8250_suspend_port - suspend one serial port
 *	@line:  serial line number
 *
 *	Suspend one serial port.
 */
void serial8250_suspend_port(int line)
{
	struct uart_8250_port *up = &serial8250_ports[line];
	struct uart_port *port = &up->port;

	if (!console_suspend_enabled && uart_console(port) &&
	    port->type != PORT_8250) {
		unsigned char canary = 0xa5;

		serial_out(up, UART_SCR, canary);
		if (serial_in(up, UART_SCR) == canary)
			up->canary = canary;
	}

	uart_suspend_port(&serial8250_reg, port);
}
EXPORT_SYMBOL(serial8250_suspend_port);

/**
 *	serial8250_resume_port - resume one serial port
 *	@line:  serial line number
 *
 *	Resume one serial port.
 */
void serial8250_resume_port(int line)
{
	struct uart_8250_port *up = &serial8250_ports[line];
	struct uart_port *port = &up->port;

	up->canary = 0;

	if (up->capabilities & UART_NATSEMI) {
		/* Ensure it's still in high speed mode */
		serial_port_out(port, UART_LCR, 0xE0);

		ns16550a_goto_highspeed(up);

		serial_port_out(port, UART_LCR, 0);
		port->uartclk = 921600*16;
	}
	uart_resume_port(&serial8250_reg, port);
}
EXPORT_SYMBOL(serial8250_resume_port);

/*
 * serial8250_register_8250_port and serial8250_unregister_port allows for
 * 16x50 serial ports to be configured at run-time, to support PCMCIA
 * modems and PCI multiport cards.
 */
static DEFINE_MUTEX(serial_mutex);

static struct uart_8250_port *serial8250_find_match_or_unused(const struct uart_port *port)
{
	int i;

	/*
	 * First, find a port entry which matches.
	 */
	for (i = 0; i < nr_uarts; i++)
		if (uart_match_port(&serial8250_ports[i].port, port))
			return &serial8250_ports[i];

	/* try line number first if still available */
	i = port->line;
	if (i < nr_uarts && serial8250_ports[i].port.type == PORT_UNKNOWN &&
			serial8250_ports[i].port.iobase == 0)
		return &serial8250_ports[i];
	/*
	 * We didn't find a matching entry, so look for the first
	 * free entry.  We look for one which hasn't been previously
	 * used (indicated by zero iobase).
	 */
	for (i = 0; i < nr_uarts; i++)
		if (serial8250_ports[i].port.type == PORT_UNKNOWN &&
		    serial8250_ports[i].port.iobase == 0)
			return &serial8250_ports[i];

	/*
	 * That also failed.  Last resort is to find any entry which
	 * doesn't have a real port associated with it.
	 */
	for (i = 0; i < nr_uarts; i++)
		if (serial8250_ports[i].port.type == PORT_UNKNOWN)
			return &serial8250_ports[i];

	return NULL;
}

static void serial_8250_overrun_backoff_work(struct work_struct *work)
{
	struct uart_8250_port *up =
	    container_of(to_delayed_work(work), struct uart_8250_port,
			 overrun_backoff);
	struct uart_port *port = &up->port;
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	up->ier |= UART_IER_RLSI | UART_IER_RDI;
	serial_out(up, UART_IER, up->ier);
	uart_port_unlock_irqrestore(port, flags);
}

/**
 *	serial8250_register_8250_port - register a serial port
 *	@up: serial port template
 *
 *	Configure the serial port specified by the request. If the
 *	port exists and is in use, it is hung up and unregistered
 *	first.
 *
 *	The port is then probed and if necessary the IRQ is autodetected
 *	If this fails an error is returned.
 *
 *	On success the port is ready to use and the line number is returned.
 */
int serial8250_register_8250_port(const struct uart_8250_port *up)
{
	struct uart_8250_port *uart;
	int ret = -ENOSPC;

	if (up->port.uartclk == 0)
		return -EINVAL;

	mutex_lock(&serial_mutex);

	uart = serial8250_find_match_or_unused(&up->port);
	if (!uart) {
		/*
		 * If the port is past the initial isa ports, initialize a new
		 * port and increment nr_uarts accordingly.
		 */
		uart = serial8250_setup_port(nr_uarts);
		if (!uart)
			goto unlock;
		nr_uarts++;
	}

	/* Check if it is CIR already. We check this below again, see there why. */
	if (uart->port.type == PORT_8250_CIR) {
		ret = -ENODEV;
		goto unlock;
	}

	if (uart->port.dev)
		uart_remove_one_port(&serial8250_reg, &uart->port);

	uart->port.ctrl_id	= up->port.ctrl_id;
	uart->port.port_id	= up->port.port_id;
	uart->port.iobase       = up->port.iobase;
	uart->port.membase      = up->port.membase;
	uart->port.irq          = up->port.irq;
	uart->port.irqflags     = up->port.irqflags;
	uart->port.uartclk      = up->port.uartclk;
	uart->port.fifosize     = up->port.fifosize;
	uart->port.regshift     = up->port.regshift;
	uart->port.iotype       = up->port.iotype;
	uart->port.flags        = up->port.flags | UPF_BOOT_AUTOCONF;
	uart->bugs		= up->bugs;
	uart->port.mapbase      = up->port.mapbase;
	uart->port.mapsize      = up->port.mapsize;
	uart->port.private_data = up->port.private_data;
	uart->tx_loadsz		= up->tx_loadsz;
	uart->capabilities	= up->capabilities;
	uart->port.throttle	= up->port.throttle;
	uart->port.unthrottle	= up->port.unthrottle;
	uart->port.rs485_config	= up->port.rs485_config;
	uart->port.rs485_supported = up->port.rs485_supported;
	uart->port.rs485	= up->port.rs485;
	uart->rs485_start_tx	= up->rs485_start_tx;
	uart->rs485_stop_tx	= up->rs485_stop_tx;
	uart->lsr_save_mask	= up->lsr_save_mask;
	uart->dma		= up->dma;

	/* Take tx_loadsz from fifosize if it wasn't set separately */
	if (uart->port.fifosize && !uart->tx_loadsz)
		uart->tx_loadsz = uart->port.fifosize;

	if (up->port.dev) {
		uart->port.dev = up->port.dev;
		ret = uart_get_rs485_mode(&uart->port);
		if (ret)
			goto err;
	}

	if (up->port.flags & UPF_FIXED_TYPE)
		uart->port.type = up->port.type;

	/*
	 * Only call mctrl_gpio_init(), if the device has no ACPI
	 * companion device
	 */
	if (!has_acpi_companion(uart->port.dev)) {
		struct mctrl_gpios *gpios = mctrl_gpio_init(&uart->port, 0);
		if (IS_ERR(gpios)) {
			ret = PTR_ERR(gpios);
			goto err;
		} else {
			uart->gpios = gpios;
		}
	}

	serial8250_set_defaults(uart);

	/* Possibly override default I/O functions.  */
	if (up->port.serial_in)
		uart->port.serial_in = up->port.serial_in;
	if (up->port.serial_out)
		uart->port.serial_out = up->port.serial_out;
	if (up->port.handle_irq)
		uart->port.handle_irq = up->port.handle_irq;
	/*  Possibly override set_termios call */
	if (up->port.set_termios)
		uart->port.set_termios = up->port.set_termios;
	if (up->port.set_ldisc)
		uart->port.set_ldisc = up->port.set_ldisc;
	if (up->port.get_mctrl)
		uart->port.get_mctrl = up->port.get_mctrl;
	if (up->port.set_mctrl)
		uart->port.set_mctrl = up->port.set_mctrl;
	if (up->port.get_divisor)
		uart->port.get_divisor = up->port.get_divisor;
	if (up->port.set_divisor)
		uart->port.set_divisor = up->port.set_divisor;
	if (up->port.startup)
		uart->port.startup = up->port.startup;
	if (up->port.shutdown)
		uart->port.shutdown = up->port.shutdown;
	if (up->port.pm)
		uart->port.pm = up->port.pm;
	if (up->port.handle_break)
		uart->port.handle_break = up->port.handle_break;
	if (up->dl_read)
		uart->dl_read = up->dl_read;
	if (up->dl_write)
		uart->dl_write = up->dl_write;

	/* Check the type (again)! It might have changed by the port.type assignment above. */
	if (uart->port.type != PORT_8250_CIR) {
		if (uart_console_registered(&uart->port))
			pm_runtime_get_sync(uart->port.dev);

		if (serial8250_isa_config != NULL)
			serial8250_isa_config(0, &uart->port,
					&uart->capabilities);

		serial8250_apply_quirks(uart);
		ret = uart_add_one_port(&serial8250_reg,
					&uart->port);
		if (ret)
			goto err;

		ret = uart->port.line;
	} else {
		dev_info(uart->port.dev,
			"skipping CIR port at 0x%lx / 0x%llx, IRQ %d\n",
			uart->port.iobase,
			(unsigned long long)uart->port.mapbase,
			uart->port.irq);

		ret = 0;
	}

	if (!uart->lsr_save_mask)
		uart->lsr_save_mask = LSR_SAVE_FLAGS;	/* Use default LSR mask */

	/* Initialise interrupt backoff work if required */
	if (up->overrun_backoff_time_ms > 0) {
		uart->overrun_backoff_time_ms =
			up->overrun_backoff_time_ms;
		INIT_DELAYED_WORK(&uart->overrun_backoff,
				serial_8250_overrun_backoff_work);
	} else {
		uart->overrun_backoff_time_ms = 0;
	}

unlock:
	mutex_unlock(&serial_mutex);

	return ret;

err:
	uart->port.dev = NULL;
	mutex_unlock(&serial_mutex);
	return ret;
}
EXPORT_SYMBOL(serial8250_register_8250_port);

/**
 *	serial8250_unregister_port - remove a 16x50 serial port at runtime
 *	@line: serial line number
 *
 *	Remove one serial port.  This may not be called from interrupt
 *	context.  We hand the port back to the our control.
 */
void serial8250_unregister_port(int line)
{
	struct uart_8250_port *uart = &serial8250_ports[line];

	mutex_lock(&serial_mutex);

	if (uart->em485) {
		unsigned long flags;

		uart_port_lock_irqsave(&uart->port, &flags);
		serial8250_em485_destroy(uart);
		uart_port_unlock_irqrestore(&uart->port, flags);
	}

	uart_remove_one_port(&serial8250_reg, &uart->port);
	if (serial8250_isa_devs) {
		uart->port.flags &= ~UPF_BOOT_AUTOCONF;
		uart->port.type = PORT_UNKNOWN;
		uart->port.dev = &serial8250_isa_devs->dev;
		uart->port.port_id = line;
		uart->capabilities = 0;
		serial8250_init_port(uart);
		serial8250_apply_quirks(uart);
		uart_add_one_port(&serial8250_reg, &uart->port);
	} else {
		uart->port.dev = NULL;
	}
	mutex_unlock(&serial_mutex);
}
EXPORT_SYMBOL(serial8250_unregister_port);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic 8250/16x50 serial driver");
