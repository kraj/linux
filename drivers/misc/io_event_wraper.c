#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>

#define DEVICE_NAME "io_event"
#define DEFAULT_BUFFER_SIZE 4096
#define MY_IOCTL_RESIZE _IOW('k', 1, int)
#define MY_IOCTL_GET_COUNT _IOR('k', 2, int)

MODULE_LICENSE("GPL");

// 每个文件描述符的私有数据结构
struct fd_private_data {
	struct kfifo fifo;
	struct mutex lock;
	bool is_closing;
	wait_queue_head_t queue;
};

static int io_event_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct fd_private_data *priv;

	priv = kzalloc(sizeof(struct fd_private_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = kfifo_alloc(&priv->fifo, DEFAULT_BUFFER_SIZE, GFP_KERNEL);
	if (ret) {
		kfree(priv);
		return ret;
	}

	mutex_init(&priv->lock);
	init_waitqueue_head(&priv->queue);
	priv->is_closing = false;

	filp->private_data = priv;
	return 0;
}

static int io_event_release(struct inode *inode, struct file *filp)
{
	struct fd_private_data *priv = filp->private_data;

	if (priv) {
		mutex_lock(&priv->lock);
		priv->is_closing = true;
		wake_up_interruptible(&priv->queue);
		mutex_unlock(&priv->lock);

		kfifo_free(&priv->fifo);
		mutex_destroy(&priv->lock);
		kfree(priv);
		filp->private_data = NULL;
	}
	return 0;
}

static int io_event_flush(struct file *filp, fl_owner_t id)
{
    struct fd_private_data *dev = filp->private_data;

    mutex_lock(&dev->lock);
	wake_up_all(&dev->queue);
    mutex_unlock(&dev->lock);

    return 0;
}

static ssize_t io_event_read(struct file *filp, char __user *buf, size_t count,
			     loff_t *f_pos)
{
	struct fd_private_data *priv = filp->private_data;
	unsigned int copied = 0;
	int ret = 0;

	if (count == 0)
		return 0;

	if (mutex_lock_interruptible(&priv->lock))
		return -ERESTARTSYS;

	while (copied < count && !priv->is_closing) {
		unsigned int avail = 0;
		size_t to_copy;

		while ((avail = kfifo_len(&priv->fifo)) == 0) {
			mutex_unlock(&priv->lock);

			if (filp->f_flags & O_NONBLOCK)
				return copied ? copied : -EAGAIN;

			if (wait_event_interruptible(priv->queue,
				    (avail = kfifo_len(&priv->fifo)) > 0 ||
					    priv->is_closing))
				return copied ? copied : -ERESTARTSYS;

			if (mutex_lock_interruptible(&priv->lock))
				return copied ? copied : -ERESTARTSYS;

			if (priv->is_closing)
				break;
		}

		if (priv->is_closing) {
			ret = -ENODEV;
			break;
		}

		to_copy = min(count - copied, (size_t)avail);

		ret = kfifo_to_user(&priv->fifo, buf + copied, to_copy, &avail);
		if (ret)
			break;

		copied += avail;
		wake_up_interruptible(&priv->queue);
	}

	mutex_unlock(&priv->lock);
	return ret ? ret : copied;
}

static ssize_t io_event_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *f_pos)
{
	struct fd_private_data *priv = filp->private_data;
	size_t copied = 0;
	int ret = 0;

	if (count == 0)
		return 0;

	if (mutex_lock_interruptible(&priv->lock))
		return -ERESTARTSYS;

	while (copied < count && !priv->is_closing) {
		unsigned int avail = 0;
		size_t to_copy;

		// 等待空间可用
		while ((avail = kfifo_avail(&priv->fifo)) == 0) {
			mutex_unlock(&priv->lock);

			if (filp->f_flags & O_NONBLOCK)
				return copied ? copied : -EAGAIN;

			if (wait_event_interruptible(priv->queue,
				    (avail = kfifo_avail(&priv->fifo)) > 0 ||
					    priv->is_closing))
				return copied ? copied : -ERESTARTSYS;

			if (mutex_lock_interruptible(&priv->lock))
				return copied ? copied : -ERESTARTSYS;

			if (priv->is_closing)
				break;
		}

		if (priv->is_closing) {
			ret = -ENODEV;
			break;
		}

		// 计算本次要拷贝的数据量
		to_copy = min(count - copied, (size_t)avail);

		ret = kfifo_from_user(&priv->fifo, buf + copied, to_copy,
				      &avail);
		if (ret)
			break;

		copied += avail;
		wake_up_interruptible(&priv->queue);
	}

	mutex_unlock(&priv->lock);
	return ret ? ret : copied;
}

static unsigned int io_event_poll(struct file *filp, poll_table *wait)
{
	struct fd_private_data *priv = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &priv->queue, wait);

	mutex_lock(&priv->lock);

	if (priv->is_closing) {
		mask |= POLLERR;
	} else {
		if (!kfifo_is_empty(&priv->fifo))
			mask |= POLLIN | POLLRDNORM;
		if (!kfifo_is_full(&priv->fifo))
			mask |= POLLOUT | POLLWRNORM;
	}

	mutex_unlock(&priv->lock);

	return mask;
}

static long io_event_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct fd_private_data *priv = filp->private_data;
	int new_size;
	int ret = 0;

	switch (cmd) {
	case MY_IOCTL_RESIZE:
		if (copy_from_user(&new_size, (int __user *)arg,
				   sizeof(new_size)))
			return -EFAULT;

		if (new_size <= 0)
			return -EINVAL;

		mutex_lock(&priv->lock);

		if (priv->is_closing) {
			ret = -ENODEV;
		} else {
			kfifo_free(&priv->fifo);
			ret = kfifo_alloc(&priv->fifo, new_size, GFP_KERNEL);
			if (ret)
				ret = -ENOMEM;
			else
				wake_up_interruptible(&priv->queue);
		}

		mutex_unlock(&priv->lock);
		break;

	case MY_IOCTL_GET_COUNT:
		mutex_lock(&priv->lock);
		if (priv->is_closing) {
			ret = -ENODEV;
		} else {
			unsigned int count = kfifo_len(&priv->fifo);
			if (copy_to_user((int __user *)arg, &count, sizeof(count))) {
				ret = -EFAULT;
			}
		}
		mutex_unlock(&priv->lock);
		break;

	default:
		return -ENOTTY;
	}

	return ret;
}

static const struct file_operations io_event_fops = {
	.owner = THIS_MODULE,
	.open = io_event_open,
	.release = io_event_release,
	.read = io_event_read,
	.write = io_event_write,
	.poll = io_event_poll,
	.flush = io_event_flush,
	.unlocked_ioctl = io_event_ioctl,
};

static struct miscdevice io_event_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &io_event_fops,
	.mode = 0666,
};

static int __init io_event_init(void)
{
	int ret = misc_register(&io_event_misc_dev);
	if (ret) {
		printk(KERN_ERR "Failed to register misc device\n");
		return ret;
	}
	printk(KERN_INFO "Registered io-event wraper device /dev/%s\n",
	       DEVICE_NAME);
	return 0;
}

static void __exit io_event_exit(void)
{
	misc_deregister(&io_event_misc_dev);
	printk(KERN_INFO "Unregistered per-fd buffer device\n");
}

module_init(io_event_init);
module_exit(io_event_exit);
