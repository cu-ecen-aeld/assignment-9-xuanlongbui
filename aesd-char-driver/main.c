/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("xuanlongbui"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    /**
     * TODO: handle read
     */    
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry = NULL;
    size_t entry_offset = 0;
    size_t bytes_available;
    size_t bytes_to_read;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &dev->buffer, *f_pos, &entry_offset);
    if (entry == NULL) {
            retval = 0;  // EOF
            goto out;
    }

    bytes_available = entry->size - entry_offset;
    bytes_to_read = min(bytes_available, count);

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += bytes_to_read;
    retval = bytes_to_read;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t total_size = 0;
    loff_t new_pos;
    uint8_t index;
    size_t i;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    index = dev->buffer.out_offs;
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        total_size += dev->buffer.entry[index].size;
        if (++index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            index = 0;
    }

    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = filp->f_pos + offset;
        break;
    case SEEK_END:
        new_pos = total_size + offset;
        break;
    default:
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    if (new_pos < 0) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = new_pos;
    mutex_unlock(&dev->lock);
    return new_pos;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *kbuf = NULL;
    bool complete ;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        goto out;

    if (copy_from_user(kbuf, buf, count)) {
        retval = -EFAULT;
        goto free_kbuf;
    }

    complete = memchr(kbuf, '\n', count) != NULL;
    
    dev->write_buffer = krealloc(dev->write_buffer, dev->write_buffer_size + count, GFP_KERNEL);
    if (!dev->write_buffer) {
        retval = -ENOMEM;
        goto free_kbuf;
    }

    memcpy(dev->write_buffer + dev->write_buffer_size, kbuf, count);
    dev->write_buffer_size += count;

    if (complete) {
        struct aesd_buffer_entry new_entry;

        new_entry.buffptr = dev->write_buffer;
        new_entry.size = dev->write_buffer_size;
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);

        dev->write_buffer = NULL;
        dev->write_buffer_size = 0;
    }

    retval = count;

free_kbuf:
    kfree(kbuf);
out:
    mutex_unlock(&dev->lock);
    return retval;
}
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    uint32_t num_cmds;
    loff_t new_pos = 0;
    uint8_t index;
    uint32_t i;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    switch (cmd) {
    case AESDCHAR_IOCSEEKTO:
        if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto)))
            return -EFAULT;

        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;

        if (dev->buffer.full)
            num_cmds = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        else
            num_cmds = (dev->buffer.in_offs - dev->buffer.out_offs +
                        AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) %
                       AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        if (seekto.write_cmd >= num_cmds) {
            mutex_unlock(&dev->lock);
            return -EINVAL;
        }

        index = (dev->buffer.out_offs + seekto.write_cmd) %
                AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        if (seekto.write_cmd_offset >= dev->buffer.entry[index].size) {
            mutex_unlock(&dev->lock);
            return -EINVAL;
        }

        /* accumulate byte offset of all commands before write_cmd */
        index = dev->buffer.out_offs;
        for (i = 0; i < seekto.write_cmd; i++) {
            new_pos += dev->buffer.entry[index].size;
            if (++index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
                index = 0;
        }
        new_pos += seekto.write_cmd_offset;

        filp->f_pos = new_pos;
        mutex_unlock(&dev->lock);
        return 0;

    default:
        return -ENOTTY;
    }
}

struct file_operations aesd_fops = {
    .owner =          THIS_MODULE,
    .llseek =         aesd_llseek,
    .read =           aesd_read,
    .write =          aesd_write,
    .open =           aesd_open,
    .release =        aesd_release,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
     
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.write_buffer = NULL;
    aesd_device.write_buffer_size = 0;

    printk(KERN_INFO "aesdchar: module loaded\n");
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    int i;

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    if (aesd_device.write_buffer)
        kfree(aesd_device.write_buffer);
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        if (aesd_device.buffer.entry[i].buffptr != NULL) {
            kfree(aesd_device.buffer.entry[i].buffptr);
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
