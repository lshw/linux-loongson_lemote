#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <loongson.h>
#include <boot_param.h>
#include <loongson_hwmon.h>

#define MAX_EMC1412_CLIENTS 4
struct i2c_client *emc1412_client[MAX_EMC1412_CLIENTS] = {NULL};

static struct device *emc1412_hwmon_dev;

static ssize_t get_hwmon_name(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t get_emc1412_label(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t get_emc1412_temp(struct device *dev,
			struct device_attribute *attr, char *buf);

static SENSOR_DEVICE_ATTR(name, S_IRUGO, get_hwmon_name, NULL, 0);

static struct attribute *emc1412_hwmon_attributes[] =
{
	&sensor_dev_attr_name.dev_attr.attr,
	NULL
};

/* Hwmon device attribute group */
static struct attribute_group emc1412_hwmon_attribute_group =
{
	.attrs = emc1412_hwmon_attributes,
};

/* Hwmon device get name */
static ssize_t get_hwmon_name(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "emc1412\n");
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, get_emc1412_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, get_emc1412_label, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, get_emc1412_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, get_emc1412_label, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, get_emc1412_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp3_label, S_IRUGO, get_emc1412_label, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, get_emc1412_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_label, S_IRUGO, get_emc1412_label, NULL, 4);

static const struct attribute *emc1412_hwmon_temp[4][3] = {
	{
		&sensor_dev_attr_temp1_input.dev_attr.attr,
		&sensor_dev_attr_temp1_label.dev_attr.attr,
		NULL
	},

	{
		&sensor_dev_attr_temp2_input.dev_attr.attr,
		&sensor_dev_attr_temp2_label.dev_attr.attr,
		NULL
	},

	{
		&sensor_dev_attr_temp3_input.dev_attr.attr,
		&sensor_dev_attr_temp3_label.dev_attr.attr,
		NULL
	},

	{
		&sensor_dev_attr_temp4_input.dev_attr.attr,
		&sensor_dev_attr_temp4_label.dev_attr.attr,
		NULL
	}
};

static int emc1412_probe(struct platform_device *dev)
{
	struct i2c_adapter *adapter = NULL;
	struct i2c_board_info info;
	int i = 0, r = 0, found = 0, id = dev->id - 1;
	struct sensor_device *sdev = (struct sensor_device *)dev->dev.platform_data;

	memset(&info, 0, sizeof(struct i2c_board_info));

	adapter = i2c_get_adapter(i++);
	while (adapter) {
		if (strncmp(adapter->name, "SMBus PIIX4", 11) == 0) {
			found = 1;
			break;
		}

		adapter = i2c_get_adapter(i++);
	}

	if (!found)
		goto fail;

	info.addr = sdev->base_addr;
	info.platform_data = dev->dev.platform_data;
	strncpy(info.type, "emc1412", I2C_NAME_SIZE);
	emc1412_client[id] = i2c_new_device(adapter, &info);
	if (emc1412_client == NULL) {
		printk(KERN_ERR "failed to attach EMC1412 sensor\n");
		goto fail;
	}

	r = sysfs_create_files(&emc1412_hwmon_dev->kobj, emc1412_hwmon_temp[id]);
	if (r)
		goto fail;

	printk(KERN_INFO "Success to attach EMC1412 sensor\n");

	return 0;

fail:
	printk(KERN_ERR "Fail to found smbus controller attach EMC1412 sensor\n");

	return r;
}

static void emc1412_shutdown(struct platform_device *dev)
{
	int id = dev->id - 1;

	emc1412_client[id] = NULL;
	msleep(15); /* Release I2C/SMBus resources */
}

/*
 * emc1412 provide 2 temprature data
 * Internal temprature: reg0.reg29
 * External temprature: reg1.reg10
 * reg0 & reg1 from 0 to 127
 * reg1 & reg10 between (0.125, 0.875)
 * to avoid use float, temprature will mult 1000
 */
int emc1412_internal_temp(int id)
{
	u8 reg;
	int temp;
	struct i2c_client *client;

	if (id < 0 || !(client = emc1412_client[id]))
		return NOT_VALID_TEMP;

	temp = i2c_smbus_read_byte_data(client, 0) * 1000;
	reg = i2c_smbus_read_byte_data(client, 0x29);
	temp += (reg >> 5) * 125;

	return temp;
}

int emc1412_external_temp(int id)
{
	u8 reg;
	int temp;
	struct i2c_client *client;

	/* not ready ??? */
	if (id < 0 || !(client = emc1412_client[id]))
		return NOT_VALID_TEMP;

	temp = i2c_smbus_read_byte_data(client, 1) * 1000;
	reg = i2c_smbus_read_byte_data(client, 0x10);
	temp += (reg >> 5) * 125;

	return temp;
}

static ssize_t get_emc1412_label(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int id = (to_sensor_dev_attr(attr))->index - 1;
	struct sensor_device *sdev = (struct sensor_device *)emc1412_client[id]->dev.platform_data;

	return sprintf(buf, "%s\n", sdev->label);
}

static ssize_t get_emc1412_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int id = (to_sensor_dev_attr(attr))->index - 1;
	int value = emc1412_external_temp(id);

	return sprintf(buf, "%d\n", value);
}

#define EMC1412_THERMAL_THRESHOLD 90000
static struct delayed_work thermal_work;

static void do_thermal_timer(struct work_struct *work)
{
	int i, value, temp_max = 0;

	for (i=0; i<MAX_EMC1412_CLIENTS; i++) {
		value = emc1412_external_temp(i);
		if (value > temp_max)
			temp_max = value;
	}

	if (temp_max <= EMC1412_THERMAL_THRESHOLD)
		schedule_delayed_work(&thermal_work, msecs_to_jiffies(5000));
	else
		orderly_poweroff(true);
}

int fixup_cpu_temp(int cpu, int cputemp)
{
	static int printed[MAX_PACKAGES] = {0, 0, 0, 0};
	int i, value, temp_min = 50000, temp_max = -20000;

	for (i=0; i<MAX_EMC1412_CLIENTS; i++) {
		value = emc1412_internal_temp(i);
		if (value == NOT_VALID_TEMP)
			continue;
		if (value < temp_min)
			temp_min = value;
		if (value > temp_max)
			temp_max = value;
	}
	for (i=0; i<MAX_EMC1412_CLIENTS; i++) {
		value = emc1412_external_temp(i);
		if (value == NOT_VALID_TEMP)
			continue;
		if (value < temp_min)
			temp_min = value;
		if (value > temp_max)
			temp_max = value;
	}

	if (temp_min > temp_max) {
		printk_once("EMC1412: No valid reference.\n");
		return cputemp;
	}
	if (cputemp < 0 && temp_max < 2000) {
		printk_once("EMC1412: No valid reference.\n");
		return cputemp;
	}

	if (cputemp < temp_min - 5000) {
		if(!printed[cpu]) {
			printed[cpu] = 1;
			printk("EMC1412: Original CPU#%d temperature too low, "
				"fixup with reference: (%d -> %d).\n",
				cpu, cputemp, temp_min - 5000);
		}
		return temp_min - 5000;
	}
	if (cputemp > temp_max + 15000) {
		if(!printed[cpu]) {
			printed[cpu] = 1;
			printk("EMC1412: Original CPU#%d temperature too high, "
				"fixup with reference: (%d -> %d).\n",
				cpu, cputemp, temp_max + 10000);
		}
		return temp_max + 15000;
	}
	if(!printed[cpu]) {
		printed[cpu] = 1;
		printk("EMC1412: Original CPU#%d temperature is OK: (%d:%d:%d).\n",
			cpu, cputemp, temp_min, temp_max);
	}

	return cputemp;
}

static struct platform_driver emc1412_driver = {
	.probe		= emc1412_probe,
	.shutdown	= emc1412_shutdown,
	.driver		= {
		.name	= "emc1412",
		.owner	= THIS_MODULE,
	},
};

static int __init emc1412_init(void)
{
	int ret;

	emc1412_hwmon_dev = hwmon_device_register(NULL);
	if (IS_ERR(emc1412_hwmon_dev)) {
		ret = -ENOMEM;
		printk(KERN_ERR "hwmon_device_register fail!\n");
		goto fail_hwmon_device_register;
	}

	ret = sysfs_create_group(&emc1412_hwmon_dev->kobj,
				&emc1412_hwmon_attribute_group);
	if (ret) {
		printk(KERN_ERR "fail to create loongson hwmon!\n");
		goto fail_sysfs_create_group_hwmon;
	}

	platform_driver_register(&emc1412_driver);
	INIT_DEFERRABLE_WORK(&thermal_work, do_thermal_timer);
	schedule_delayed_work(&thermal_work, msecs_to_jiffies(20000));

	return 0;

fail_sysfs_create_group_hwmon:
	hwmon_device_unregister(emc1412_hwmon_dev);

fail_hwmon_device_register:
	return ret;
}

static void __exit emc1412_exit(void)
{
	cancel_delayed_work_sync(&thermal_work);
	platform_driver_unregister(&emc1412_driver);
	sysfs_remove_group(&emc1412_hwmon_dev->kobj,
				&emc1412_hwmon_attribute_group);
	hwmon_device_unregister(emc1412_hwmon_dev);
}

late_initcall(emc1412_init);
module_exit(emc1412_exit);

MODULE_AUTHOR("Yu Xiang <xiangy@lemote.com>");
MODULE_AUTHOR("Huacai Chen <chenhc@lemote.com>");
MODULE_DESCRIPTION("EMC1412 driver");
MODULE_LICENSE("GPL");
