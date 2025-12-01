// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 *
 * Dong Yan <yand@zhcomputing.com>
 */

#define pr_fmt(fmt)KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/mutex.h>

#define ZH_DVFS_MAX_REGULATORS 2

/* SOC specific data */
struct zh_cpufreq_bus_opp {
	unsigned long long bus_clk_freq;
	unsigned long long pic_clk_freq;
	unsigned long long cfg_clk_freq;
	unsigned long long com_clk_freq;
	unsigned long long apb_clk_freq;
};

struct zh_cpufreq_soc_data {
	struct device *dev;
	struct clk *bus_clk;
	struct clk *pic_clk;
	struct clk *cfg_clk;
	struct clk *com_clk;
	struct clk *apb_clk;
	struct zh_cpufreq_bus_opp *bus_opp;
	unsigned int num_bus_opp;
};

/* cluster specific data */
struct zh_cpufreq_cluster_data {
	cpumask_var_t cpus;
	struct device *cpu_dev;
	struct regulator *dvdd_cpu;
	struct regulator *dvddm_cpu;
	struct regulator *dvdd_cpu_p;
	struct list_head node;
	int opp_token;
	struct cpufreq_frequency_table *freq_table;
	struct clk *pll_mux;
	struct zh_cpufreq_soc_data *soc;
	unsigned int num_opps;
};

static LIST_HEAD(info_list);

static struct freq_attr *zh_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,   /* Extra space for boost-attr if required */
	NULL,
};

static struct zh_cpufreq_cluster_data *zh_cpufreq_cluster_data_lookup(int cpu)
{
	struct zh_cpufreq_cluster_data *cluster;

	list_for_each_entry(cluster, &info_list, node) {
		if (cpumask_test_cpu(cpu, cluster->cpus))
			return cluster;
	}

	return NULL;
}

static int zh_cpufreq_online(struct cpufreq_policy *policy)
{
	/* TODO: hotplug specific actions */
	return 0;
}

static int zh_cpufreq_offline(struct cpufreq_policy *policy)
{
	/* TODO: hotplug specific actions */
	return 0;
}

static int zh_cpufreq_init(struct cpufreq_policy *policy)
{
	struct zh_cpufreq_cluster_data *cluster;
	struct device *cpu_dev;
	unsigned int transition_latency;
	int ret;

	cluster = zh_cpufreq_cluster_data_lookup(policy->cpu);
	if (!cluster) {
		pr_err("failed to find data for cpu%d\n", policy->cpu);
		return -ENODEV;
	}
	cpu_dev = cluster->cpu_dev;

	transition_latency = dev_pm_opp_get_max_transition_latency(cpu_dev);
	if (!transition_latency)
		transition_latency = CPUFREQ_ETERNAL;

	cpumask_copy(policy->cpus, cluster->cpus);
	policy->driver_data = cluster;
	policy->clk = cluster->pll_mux;
	policy->freq_table = cluster->freq_table;
	policy->suspend_freq = dev_pm_opp_get_suspend_opp_freq(cpu_dev) / 1000;
	policy->cpuinfo.transition_latency = transition_latency;
	policy->dvfs_possible_from_any_cpu = true;

	/* Support turbo/boost mode */
	if (policy_has_boost_freq(policy)) {
		/* This gets disabled by core on driver unregister */
		ret = cpufreq_enable_boost_support();
		if (ret)
			return ret;
		zh_cpufreq_attr[1] = &cpufreq_freq_attr_scaling_boost_freqs;
	}

	return 0;
}

static int zh_cpufreq_exit(struct cpufreq_policy *policy)
{
	clk_put(policy->clk);
	return 0;
}

static int zh_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct zh_cpufreq_cluster_data *cluster = policy->driver_data;
	struct zh_cpufreq_soc_data *soc = cluster->soc;
	unsigned long target_freq = policy->freq_table[index].frequency;
	unsigned int current_freq = policy->cur;

	if (target_freq < current_freq && policy->cpu == 0) {
		clk_set_rate(soc->bus_clk, soc->bus_opp[index].bus_clk_freq);
		clk_set_rate(soc->pic_clk, soc->bus_opp[index].pic_clk_freq);
		clk_set_rate(soc->cfg_clk, soc->bus_opp[index].cfg_clk_freq);
		clk_set_rate(soc->com_clk, soc->bus_opp[index].com_clk_freq);
		clk_set_rate(soc->apb_clk, soc->bus_opp[index].apb_clk_freq);
	}

	clk_set_rate(cluster->pll_mux, 1000000000);
	dev_pm_opp_set_rate(cluster->cpu_dev, target_freq * 1000);

	if (target_freq > current_freq && policy->cpu == 0) {
		clk_set_rate(soc->bus_clk, soc->bus_opp[index].bus_clk_freq);
		clk_set_rate(soc->pic_clk, soc->bus_opp[index].pic_clk_freq);
		clk_set_rate(soc->cfg_clk, soc->bus_opp[index].cfg_clk_freq);
		clk_set_rate(soc->com_clk, soc->bus_opp[index].com_clk_freq);
		clk_set_rate(soc->apb_clk, soc->bus_opp[index].apb_clk_freq);
	}

	return 0;
}

static struct cpufreq_driver zh_cpufreq_driver = {
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK | CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
		 CPUFREQ_IS_COOLING_DEV,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = zh_set_target,
	.get = cpufreq_generic_get,
	.init = zh_cpufreq_init,
	.exit = zh_cpufreq_exit,
	.online = zh_cpufreq_online,
	.offline = zh_cpufreq_offline,
	.register_em = cpufreq_register_em_with_opp,
	.name = "zh-cpufreq",
	.attr = zh_cpufreq_attr,
	.suspend = cpufreq_generic_suspend,
};

static int find_supply_name(struct device *dev, struct device_node *cpu_np, const char ***reg_names_out)
{
	struct property *prop;
	const char *prop_name;
	size_t len;
	int count = 0;
	const char **names_array;

	if (!cpu_np)
		return -EINVAL;

	names_array = devm_kzalloc(dev, (ZH_DVFS_MAX_REGULATORS + 1) * sizeof(char *), GFP_KERNEL);
	if (!names_array)
		return -ENOMEM;

	for_each_property_of_node(cpu_np, prop) {
		prop_name = prop->name;
		len = strlen(prop_name);

		if (len > 7 && strcmp(prop_name + len - 7, "-supply") == 0) {
			if (count >= ZH_DVFS_MAX_REGULATORS) {
				pr_err("Too many regulators defined!\n");
				return -ENOMEM;
			}
			char *reg_name_buf = devm_kzalloc(dev, len - 7 + 1, GFP_KERNEL);
			if (!reg_name_buf)
				return -ENOMEM;

			strncpy(reg_name_buf, prop_name, len - 7);
			reg_name_buf[len - 7] = '\0';
			names_array[count] = reg_name_buf;
			count++;
		}
	}
	*reg_names_out = names_array;

	return count;
}

static int zh_opp_config_regulators(struct device *dev,
			struct dev_pm_opp *old_opp, struct dev_pm_opp *new_opp,
			struct regulator **regulators, unsigned int count)
{
	int ret;
	struct dev_pm_opp_supply new_supplies[2];

	/* We must have two regulators here */
	WARN_ON(count != 2);

	/* Fetch supplies and freq information from OPP core */
	ret = dev_pm_opp_get_supplies(new_opp, new_supplies);
	WARN_ON(ret);

	for (int i = 0; i < count; i++) {
		if (IS_ERR(regulators[i])) {
			dev_dbg(dev, "%s: regulator not available: %ld\n", __func__,
				PTR_ERR(regulators[i]));
			return 0;
		}
		ret = regulator_set_voltage_triplet(regulators[i], new_supplies[i].u_volt_min,
						new_supplies[i].u_volt, new_supplies[i].u_volt_max);
		if (ret)
			dev_err(dev, "%s: failed to set voltage (%lu %lu %lu mV): %d\n",
				__func__, new_supplies[i].u_volt_min, new_supplies[i].u_volt,
				new_supplies[i].u_volt_max, ret);
	}

	return 0;
}

static int find_bus_info(struct zh_cpufreq_soc_data *soc)
{
	struct device_node *cpu_np = NULL;
	struct device_node *opp_table_np = NULL;
	struct device_node *opp_np = NULL;
	struct device *cpu_dev = get_cpu_device(0);
	struct device *dev = soc->dev;
	int i = 0;

	soc->bus_clk = clk_get(dev, "bus_clk");
	if (IS_ERR(soc->bus_clk)) {
		dev_err(dev, "failed to get bus_clk\n");
		return PTR_ERR(soc->bus_clk);
	}

	soc->pic_clk = clk_get(dev, "pic_clk");
	if (IS_ERR(soc->pic_clk)) {
		dev_err(dev, "failed to get pic_clk\n");
		return PTR_ERR(soc->pic_clk);
	}

	soc->cfg_clk = clk_get(dev, "cfg_clk");
	if (IS_ERR(soc->cfg_clk)) {
		dev_err(dev, "failed to get cfg_clk\n");
		return PTR_ERR(soc->cfg_clk);
	}

	soc->com_clk = clk_get(dev, "com_clk");
	if (IS_ERR(soc->com_clk)) {
		dev_err(dev, "failed to get com_clk\n");
		return PTR_ERR(soc->com_clk);
	}

	soc->apb_clk = clk_get(dev, "apb_clk");
	if (IS_ERR(soc->apb_clk)) {
		dev_err(dev, "failed to get apb_clk\n");
		return PTR_ERR(soc->apb_clk);
	}

	cpu_np = of_get_cpu_node(0, NULL);
	if (!cpu_np) {
		dev_err(dev, "Failed to get CPU device node\n");
		return -ENODEV;
    	}

	opp_table_np = of_parse_phandle(cpu_np, "operating-points-v2", 0);
	if (!opp_table_np) {
		dev_err(dev, "Failed to get CPU opp_table\n");
		return -ENODEV;
    	}

	soc->num_bus_opp = dev_pm_opp_get_opp_count(cpu_dev);
	if (!soc->num_bus_opp) {
		dev_err(dev, "No OPPs found\n");
		return -ENODEV;
    	}

	soc->bus_opp = devm_kzalloc(dev, sizeof(struct zh_cpufreq_bus_opp) * soc->num_bus_opp, GFP_KERNEL);
	if (!soc->bus_opp)
		return -ENOMEM;

	for_each_child_of_node(opp_table_np, opp_np) {
		if (of_property_read_u64(opp_np, "bus-clk-hz", &soc->bus_opp[i].bus_clk_freq))
			dev_warn(dev, "Missing 'bus-clk-hz' for OPP%d\n", i);

		if (of_property_read_u64(opp_np, "pic-clk-hz", &soc->bus_opp[i].pic_clk_freq))
			dev_warn(dev, "Missing 'pic-clk-hz' for OPP%d\n", i);

		if (of_property_read_u64(opp_np, "cfg-clk-hz", &soc->bus_opp[i].cfg_clk_freq))
			dev_warn(dev, "Missing 'cfg-clk-hz' for OPP%d\n", i);

		if (of_property_read_u64(opp_np, "com-clk-hz", &soc->bus_opp[i].com_clk_freq))
			dev_warn(dev, "Missing 'com-clk-hz' for OPP%d\n", i);

		if (of_property_read_u64(opp_np, "apb-clk-hz", &soc->bus_opp[i].apb_clk_freq))
			dev_warn(dev, "Missing 'apb-clk-hz' for OPP%d\n", i);

		i++;
	}

	return 0;
}

static int zh_cpufreq_prepare_cluster(struct device *dev, int cpu)
{
	struct zh_cpufreq_cluster_data *cluster;
	struct device *cpu_dev;
	const char **reg_names = NULL;
	int num_regulators;
	int ret;

	cluster = zh_cpufreq_cluster_data_lookup(cpu);
	if (cluster)
		return 0; // dvfs cluster of this cpu already existed in case of hotplug.

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return -EPROBE_DEFER;

	cluster = devm_kzalloc(dev, sizeof(*cluster), GFP_KERNEL);
	if (!cluster) {
		return -ENOMEM;
	}
	cluster->soc = dev_get_drvdata(dev);

	cluster->cpu_dev = cpu_dev;

	cluster->pll_mux = clk_get(cpu_dev, NULL);
	if (IS_ERR(cluster->pll_mux)) {
		ret = PTR_ERR(cluster->pll_mux);
		dev_err(cpu_dev, "%s: failed to get clk: %d\n", __func__, ret);
		return ret;
	}

	if (!alloc_cpumask_var(&cluster->cpus, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * OPP layer will be taking care of regulators now, but it needs to know
	 * the name of the regulator first.
	 */
	num_regulators = find_supply_name(dev, of_get_cpu_node(cpu, NULL), &reg_names);
	if (num_regulators <= 0) {
		of_node_put(of_get_cpu_node(cpu, NULL));
		ret = dev_err_probe(dev, num_regulators,
					"failed to find regulators for cpu%d\n", cpu);
		goto free_cpumask;
	}

	cluster->opp_token = dev_pm_opp_set_regulators(cpu_dev, reg_names);
	if (cluster->opp_token < 0) {
		ret = dev_err_probe(dev, cluster->opp_token,
					"failed to set regulators\n");
		goto free_cpumask;
	}

	ret = dev_pm_opp_set_config_regulators(cpu_dev, zh_opp_config_regulators);
	if (ret < 0) {
		dev_err(dev, "failed to set config_regulators callback ret=%d\n", ret);
		goto free_cpumask;
	}

	/* Get OPP-sharing information from "operating-points-v2" bindings .
	 * After this point, cpumask will include all cpus within the same cluster.
	 */
	ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, cluster->cpus);
	if (ret) {
		goto out_regulator;
	}

	/*
	 * Initialize OPP tables for all priv->cpus from device tree. They will be shared by
	 * all CPUs which have marked their CPUs shared with OPP bindings.
	 */
	ret = dev_pm_opp_of_cpumask_add_table(cluster->cpus);
	if (ret) {
		goto out_table;
	}

	/*
	 * The OPP table must be initialized by this point.
	 */
	cluster->num_opps = dev_pm_opp_get_opp_count(cpu_dev);
	if (cluster->num_opps <= 0) {
		dev_err(cpu_dev, "OPP table can't be empty\n");
		ret = -ENODEV;
		goto out_table;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &cluster->freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_table;
	}

	list_add(&cluster->node, &info_list);
	
	return 0;

out_table:
	dev_pm_opp_of_cpumask_remove_table(cluster->cpus);
out_regulator:
	dev_pm_opp_put_regulators(cluster->opp_token);
free_cpumask:
	free_cpumask_var(cluster->cpus);
	return ret;
}

static void zh_cpufreq_release(void)
{
	struct zh_cpufreq_cluster_data *cluster, *tmp;

	list_for_each_entry_safe(cluster, tmp, &info_list, node) {
		dev_pm_opp_free_cpufreq_table(cluster->cpu_dev, &cluster->freq_table);
		dev_pm_opp_of_cpumask_remove_table(cluster->cpus);
		dev_pm_opp_put_regulators(cluster->opp_token);
		free_cpumask_var(cluster->cpus);
		list_del(&cluster->node);
	}
}

static int zh_cpufreq_probe(struct platform_device *pdev)
{
	struct zh_cpufreq_soc_data *soc;
	struct device *dev = &pdev->dev;
	int cpu, ret;
	
	soc = devm_kzalloc(dev, sizeof(*soc), GFP_KERNEL);
	if (!soc)
		return -ENOMEM;
	soc->dev = dev;
	dev_set_drvdata(dev, soc);

	for_each_possible_cpu(cpu) {
		ret = zh_cpufreq_prepare_cluster(dev, cpu);
		if (ret)
			goto err;
	}

	ret = find_bus_info(soc);
	if (ret < 0) {
		dev_err(dev, "failed to find bus clk opp %d\n", ret);
		goto err;
	}	

	ret = cpufreq_register_driver(&zh_cpufreq_driver);
	if (ret) {
		dev_err(dev, "failed register driver: %d\n", ret);
		goto err;
	}

	return 0;

err:
	zh_cpufreq_release();
	return ret;
}

static const struct of_device_id zh_cpufreq_of_match[] = {
	{ .compatible = "zhihe,a210-cpufreq"},
	{}
};
MODULE_DEVICE_TABLE(of, zh_cpufreq_of_match);

static struct platform_driver zh_cpufreq_platdrv = {
	.driver = {
		.name	= "zh-cpufreq",
		.of_match_table = of_match_ptr(zh_cpufreq_of_match),
	},
	.probe		= zh_cpufreq_probe,
};

module_platform_driver(zh_cpufreq_platdrv);

MODULE_AUTHOR("Dong Yan <yand@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe cpufreq driver");
MODULE_LICENSE("GPL v2");
