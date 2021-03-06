/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/circ_buf.h>
#include <linux/coresight.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "coresight-priv.h"
#include "coresight-tmc.h"

/**
 * struct cs_etr_buffer - keep track of a recording session' specifics
 * @tmc:	generic portion of the TMC buffers
 * @paddr:	the physical address of a DMA'able contiguous memory area
 * @vaddr:	the virtual address associated to @paddr
 * @size:	how much memory we have, starting at @paddr
 * @dev:	the device @vaddr has been tied to
 */
struct cs_etr_buffers {
	struct cs_buffers	tmc;
	dma_addr_t		paddr;
	void __iomem		*vaddr;
	u32			size;
	struct device		*dev;
};

static void tmc_etr_enable_hw(struct tmc_drvdata *drvdata)
{
	u32 axictl, sts;

	/* Zero out the memory to help with debug */
	memset(drvdata->vaddr, 0, drvdata->size);

	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(drvdata->size / 4, drvdata->base + TMC_RSZ);
	writel_relaxed(TMC_MODE_CIRCULAR_BUFFER, drvdata->base + TMC_MODE);

	axictl = readl_relaxed(drvdata->base + TMC_AXICTL);
	axictl &= ~TMC_AXICTL_CLEAR_MASK;
	axictl |= (TMC_AXICTL_PROT_CTL_B1 | TMC_AXICTL_WR_BURST_16);
	axictl |= TMC_AXICTL_AXCACHE_OS;

	if (tmc_etr_has_cap(drvdata, TMC_ETR_AXI_ARCACHE)) {
		axictl &= ~TMC_AXICTL_ARCACHE_MASK;
		axictl |= TMC_AXICTL_ARCACHE_OS;
	}

	writel_relaxed(axictl, drvdata->base + TMC_AXICTL);
	tmc_write_dba(drvdata, drvdata->paddr);
	/*
	 * If the TMC pointers must be programmed before the session,
	 * we have to set it properly (i.e, RRP/RWP to base address and
	 * STS to "not full").
	 */
	if (tmc_etr_has_cap(drvdata, TMC_ETR_SAVE_RESTORE)) {
		tmc_write_rrp(drvdata, drvdata->paddr);
		tmc_write_rwp(drvdata, drvdata->paddr);
		sts = readl_relaxed(drvdata->base + TMC_STS) & ~TMC_STS_FULL;
		writel_relaxed(sts, drvdata->base + TMC_STS);
	}

	writel_relaxed(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI |
		       TMC_FFCR_FON_FLIN | TMC_FFCR_FON_TRIG_EVT |
		       TMC_FFCR_TRIGON_TRIGIN,
		       drvdata->base + TMC_FFCR);
	writel_relaxed(drvdata->trigger_cntr, drvdata->base + TMC_TRG);
	tmc_enable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static void tmc_etr_dump_hw(struct tmc_drvdata *drvdata)
{
	const u32 *barrier;
	u32 val;
	u32 *temp;
	u64 rwp;

	rwp = tmc_read_rwp(drvdata);
	val = readl_relaxed(drvdata->base + TMC_STS);

	/*
	 * Adjust the buffer to point to the beginning of the trace data
	 * and update the available trace data.
	 */
	if (val & TMC_STS_FULL) {
		drvdata->buf = drvdata->vaddr + rwp - drvdata->paddr;
		drvdata->len = drvdata->size;

		barrier = barrier_pkt;
		temp = (u32 *)drvdata->buf;

		while (*barrier) {
			*temp = *barrier;
			temp++;
			barrier++;
		}

	} else {
		drvdata->buf = drvdata->vaddr;
		drvdata->len = rwp - drvdata->paddr;
	}
}

static void tmc_etr_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	/*
	 * When operating in sysFS mode the content of the buffer needs to be
	 * read before the TMC is disabled.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
		tmc_etr_dump_hw(drvdata);
	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static int tmc_enable_etr_sink_sysfs(struct coresight_device *csdev)
{
	int ret = 0;
	bool used = false;
	unsigned long flags;
	void __iomem *vaddr = NULL;
	dma_addr_t paddr;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);


	/*
	 * If we don't have a buffer release the lock and allocate memory.
	 * Otherwise keep the lock and move along.
	 */
	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (!drvdata->vaddr) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);

		/*
		 * Contiguous  memory can't be allocated while a spinlock is
		 * held.  As such allocate memory here and free it if a buffer
		 * has already been allocated (from a previous session).
		 */
		vaddr = dma_alloc_coherent(drvdata->dev, drvdata->size,
					   &paddr, GFP_KERNEL);
		if (!vaddr)
			return -ENOMEM;

		/* Let's try again */
		spin_lock_irqsave(&drvdata->spinlock, flags);
	}

	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * In sysFS mode we can have multiple writers per sink.  Since this
	 * sink is already enabled no memory is needed and the HW need not be
	 * touched.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
		goto out;

	/*
	 * If drvdata::buf == NULL, use the memory allocated above.
	 * Otherwise a buffer still exists from a previous session, so
	 * simply use that.
	 */
	if (drvdata->buf == NULL) {
		used = true;
		drvdata->vaddr = vaddr;
		drvdata->paddr = paddr;
		drvdata->buf = drvdata->vaddr;
	}

	drvdata->mode = CS_MODE_SYSFS;
	tmc_etr_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free memory outside the spinlock if need be */
	if (!used && vaddr)
		dma_free_coherent(drvdata->dev, drvdata->size, vaddr, paddr);

	if (!ret)
		dev_dbg(drvdata->dev, "TMC-ETR enabled\n");

	return ret;
}

static int tmc_enable_etr_sink_perf(struct coresight_device *csdev)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * In Perf mode there can be only one writer per sink.  There
	 * is also no need to continue if the ETR is already operated
	 * from sysFS.
	 */
	if (drvdata->mode != CS_MODE_DISABLED) {
		ret = -EINVAL;
		goto out;
	}

	drvdata->mode = CS_MODE_PERF;
	tmc_etr_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

static int tmc_enable_etr_sink(struct coresight_device *csdev, u32 mode)
{
	switch (mode) {
	case CS_MODE_SYSFS:
		return tmc_enable_etr_sink_sysfs(csdev);
	case CS_MODE_PERF:
		return tmc_enable_etr_sink_perf(csdev);
	}

	/* We shouldn't be here */
	return -EINVAL;
}

static void tmc_disable_etr_sink(struct coresight_device *csdev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return;
	}

	/* Disable the TMC only if it needs to */
	if (drvdata->mode != CS_MODE_DISABLED) {
		tmc_etr_disable_hw(drvdata);
		drvdata->mode = CS_MODE_DISABLED;
	}

	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_dbg(drvdata->dev, "TMC-ETR disabled\n");
}

static void *tmc_alloc_etr_buffer(struct coresight_device *csdev, int cpu,
				  void **pages, int nr_pages, bool overwrite)
{
	int node;
	struct cs_etr_buffers *buf;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (cpu == -1)
		cpu = smp_processor_id();
	node = cpu_to_node(cpu);

	/* Allocate memory structure for interaction with Perf */
	buf = kzalloc_node(sizeof(struct cs_etr_buffers), GFP_KERNEL, node);
	if (!buf)
		return NULL;

	buf->dev = drvdata->dev;
	buf->size = drvdata->size;
	buf->vaddr = dma_alloc_coherent(buf->dev, buf->size,
					&buf->paddr, GFP_KERNEL);
	if (!buf->vaddr) {
		kfree(buf);
		return NULL;
	}

	buf->tmc.snapshot = overwrite;
	buf->tmc.nr_pages = nr_pages;
	buf->tmc.data_pages = pages;

	return buf;
}

static void tmc_free_etr_buffer(void *config)
{
	struct cs_etr_buffers *buf = config;

	dma_free_coherent(buf->dev, buf->size, buf->vaddr, buf->paddr);
	kfree(buf);
}

static int tmc_set_etr_buffer(struct coresight_device *csdev,
			      struct perf_output_handle *handle,
			      void *sink_config)
{
	int ret = 0;
	unsigned long head;
	struct cs_etr_buffers *buf = sink_config;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/* wrap head around to the amount of space we have */
	head = handle->head & ((buf->tmc.nr_pages << PAGE_SHIFT) - 1);

	/* find the page to write to */
	buf->tmc.cur = head / PAGE_SIZE;

	/* and offset within that page */
	buf->tmc.offset = head % PAGE_SIZE;

	local_set(&buf->tmc.data_size, 0);

	/* Tell the HW where to put the trace data */
	drvdata->vaddr = buf->vaddr;
	drvdata->paddr = buf->paddr;
	memset(drvdata->vaddr, 0, drvdata->size);

	return ret;
}

static unsigned long tmc_reset_etr_buffer(struct coresight_device *csdev,
					  struct perf_output_handle *handle,
					  void *sink_config)
{
	long size = 0;
	struct cs_etr_buffers *buf = sink_config;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (buf) {
		/*
		 * In snapshot mode ->data_size holds the new address of the
		 * ring buffer's head.  The size itself is the whole address
		 * range since we want the latest information.
		 */
		if (buf->tmc.snapshot) {
			size = buf->tmc.nr_pages << PAGE_SHIFT;
			handle->head = local_xchg(&buf->tmc.data_size, size);
		}

		/*
		 * Tell the tracer PMU how much we got in this run and if
		 * something went wrong along the way.  Nobody else can use
		 * this cs_etr_buffers instance until we are done.  As such
		 * resetting parameters here and squaring off with the ring
		 * buffer API in the tracer PMU is fine.
		 */
		size = local_xchg(&buf->tmc.data_size, 0);
	}

	/* Get ready for another run */
	drvdata->vaddr = NULL;
	drvdata->paddr = 0;

	return size;
}

static void tmc_update_etr_buffer(struct coresight_device *csdev,
				  struct perf_output_handle *handle,
				  void *sink_config)
{
	bool lost = false;
	int cur;
	const u32 *barrier;
	u32 *buf_ptr;
	u64 read_ptr, write_ptr;
	u32 status, to_read, to_copy;
	unsigned long offset, r_offset;
	struct cs_buffers *buf = sink_config;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (!buf)
		return;

	/* This shouldn't happen */
	if (WARN_ON_ONCE(drvdata->mode != CS_MODE_PERF))
		return;

	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);

	read_ptr = tmc_read_rrp(drvdata);
	write_ptr = tmc_read_rwp(drvdata);

	/*
	 * Get a hold of the status register and see if a wrap around
	 * has occurred.  If so adjust things accordingly.
	 */
	status = readl_relaxed(drvdata->base + TMC_STS);
	if (status & TMC_STS_FULL) {
		lost = true;
		to_read = drvdata->size;
	} else {
		to_read = CIRC_CNT(write_ptr, read_ptr, drvdata->size);
	}

	/*
	 * The TMC RAM buffer may be bigger than the space available in the
	 * perf ring buffer (handle->size).  If so advance the RRP so that we
	 * get the latest trace data.
	 */
	if (to_read > handle->size) {
		u64 mask = 0;

		/*
		 * The value written to RRP must be byte-address aligned to
		 * the width of the trace memory databus _and_ to a frame
		 * boundary (16 byte), whichever is the biggest. For example,
		 * for 32-bit, 64-bit and 128-bit wide trace memory, the four
		 * LSBs must be 0s. For 256-bit wide trace memory, the five
		 * LSBs must be 0s.
		 */
		switch (drvdata->memwidth) {
		case TMC_MEM_INTF_WIDTH_32BITS:
		case TMC_MEM_INTF_WIDTH_64BITS:
		case TMC_MEM_INTF_WIDTH_128BITS:
			mask = GENMASK_ULL(63, 5);
			break;
		case TMC_MEM_INTF_WIDTH_256BITS:
			mask = GENMASK_ULL(63, 6);
			break;
		}

		/*
		 * Make sure the new size is aligned in accordance with the
		 * requirement explained above.
		 */
		to_read = handle->size & mask;
		/* Move the RAM read pointer up */
		read_ptr = (write_ptr + drvdata->size) - to_read;
		/* Make sure we are still within our limits */
		if (read_ptr > (drvdata->paddr + (drvdata->size - 1)))
			read_ptr -= drvdata->size;
		/* Tell the HW */
		tmc_write_rrp(drvdata, read_ptr);
		lost = true;
	}

	if (lost)
		perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);

	cur = buf->cur;
	offset = buf->offset;
	barrier = barrier_pkt;

	if (lost) {
		buf_ptr = buf->data_pages[cur] + offset;
		while (*barrier) {
			*buf_ptr = *barrier;
			barrier++;

			offset += 4;
			if (offset >= PAGE_SIZE) {
				offset = 0;
				cur++;
				/* wrap around at the end of the buffer */
				cur &= buf->nr_pages - 1;
			}
		}
	}

	/* Convert read pointer to buffer offset */
	r_offset = read_ptr - drvdata->paddr;
	to_copy = to_read;
	while (to_copy > 0) {
		/* Copy chunk that is minimum of data available,
		 * data before ETR wrap point, space in output chunk */
		u32 before_wrap = drvdata->size - r_offset;
		u32 out_space = PAGE_SIZE - offset;
		u32 this_chunk = min(min(to_copy, before_wrap), out_space);

		memcpy(buf->data_pages[cur] + offset, drvdata->vaddr + r_offset, this_chunk);

		to_copy -= this_chunk;
		offset += this_chunk;
		if (offset >= PAGE_SIZE) {
			offset = 0;
			cur++;
			/* wrap around at the end of the buffer */
			cur &= buf->nr_pages - 1;
		}
		r_offset += this_chunk;
		if (r_offset >= drvdata->size)
			/* Wrap to start of buffer */
			r_offset -= drvdata->size;
	}

	/*
	 * In snapshot mode all we have to do is communicate to
	 * perf_aux_output_end() the address of the current head.  In full
	 * trace mode the same function expects a size to move rb->aux_head
	 * forward.
	 */
	if (buf->snapshot)
		local_set(&buf->data_size, (cur * PAGE_SIZE) + offset);
	else
		local_add(to_read, &buf->data_size);

	CS_LOCK(drvdata->base);
}

static const struct coresight_ops_sink tmc_etr_sink_ops = {
	.enable		= tmc_enable_etr_sink,
	.disable	= tmc_disable_etr_sink,
	.alloc_buffer	= tmc_alloc_etr_buffer,
	.free_buffer	= tmc_free_etr_buffer,
	.set_buffer	= tmc_set_etr_buffer,
	.reset_buffer	= tmc_reset_etr_buffer,
	.update_buffer	= tmc_update_etr_buffer,
};

const struct coresight_ops tmc_etr_cs_ops = {
	.sink_ops	= &tmc_etr_sink_ops,
};

int tmc_read_prepare_etr(struct tmc_drvdata *drvdata)
{
	int ret = 0;
	unsigned long flags;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	/* Don't interfere if operated from Perf */
	if (drvdata->mode == CS_MODE_PERF) {
		ret = -EINVAL;
		goto out;
	}

	/* If drvdata::buf is NULL the trace data has been read already */
	if (drvdata->buf == NULL) {
		ret = -EINVAL;
		goto out;
	}

	/* Disable the TMC if need be */
	if (drvdata->mode == CS_MODE_SYSFS)
		tmc_etr_disable_hw(drvdata);

	drvdata->reading = true;
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

int tmc_read_unprepare_etr(struct tmc_drvdata *drvdata)
{
	unsigned long flags;
	dma_addr_t paddr;
	void __iomem *vaddr = NULL;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* RE-enable the TMC if need be */
	if (drvdata->mode == CS_MODE_SYSFS) {
		/*
		 * The trace run will continue with the same allocated trace
		 * buffer. The trace buffer is cleared in tmc_etr_enable_hw(),
		 * so we don't have to explicitly clear it. Also, since the
		 * tracer is still enabled drvdata::buf can't be NULL.
		 */
		tmc_etr_enable_hw(drvdata);
	} else {
		/*
		 * The ETR is not tracing and the buffer was just read.
		 * As such prepare to free the trace buffer.
		 */
		vaddr = drvdata->vaddr;
		paddr = drvdata->paddr;
		drvdata->buf = drvdata->vaddr = NULL;
	}

	drvdata->reading = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free allocated memory out side of the spinlock */
	if (vaddr)
		dma_free_coherent(drvdata->dev, drvdata->size, vaddr, paddr);

	return 0;
}
