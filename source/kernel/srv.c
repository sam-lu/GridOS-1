/**
 *   See the readme.txt at the root directory of this project for the idea and originality of this operating system.
 *   See the license.txt at the root directory of this project for the copyright information about this file and project.
 *
 *   Wuxin
 *   内核 针对用户层的接口
 */

#include <types.h>
#include <linkage.h>

#include <section.h>
#include <process.h>
#include <sync.h>
#include <exe.h>
#include <handle.h>

#include <asm/abicall.h>

#include <kernel/ke_srv.h>

#include "string.h"
#include "../source/libs/grid/include/sys/ke_req.h"

bool ke_validate_user_buffer(void * buffer, size_t size, bool rw)
{
	return true;
}

/************************************************************************/
/* Process                                                              */
/************************************************************************/
/**
 @brief New process send a startup signal
 
 We copy the startup cmdline to the process
 */
static void process_startup(struct sysreq_process_startup * req)
{
	xstring __user cmdline = req->cmdline_buffer;
	
	if (req->func == SYSREQ_PROCESS_STARTUP_FUNC_START)
	{
		/* The user buffer can be written ? */
		if (ke_validate_user_buffer(cmdline, SYSREQ_PROCESS_STARTUP_MAX_SIZE, true) == false)
			return;
		memcpy(cmdline, (void*)(kt_arch_get_sp0(kt_current()) - KT_ARCH_THREAD_CP0_STACK_SIZE),
			   SYSREQ_PROCESS_STARTUP_MAX_SIZE);
		printk("cmdline is %s.\n", cmdline);
	}
	else if (req->func == SYSREQ_PROCESS_STARTUP_FUNC_END)
	{
		/* Killing main thread will trigger the process destroying */
		kt_delete_current();
	}
}

/**
 @brief dynamic linker ops
 */
static ke_handle process_ld(struct sysreq_process_ld * req)
{
	ke_handle handle;
	xstring module_name;
	
	switch (req->function_type)
	{
		case SYSREQ_PROCESS_OPEN_EXE:
		{
			struct ko_exe *image;
			
			module_name = req->name;
			
			/* Validate the user buffer */
			if (ke_validate_user_buffer(req->context, req->context_length, true) == false)
				goto ld_0_err;
			if (ke_validate_user_buffer(module_name, strlen(module_name), false) == false)
				goto ld_0_err;
			if (ke_validate_user_buffer(&req->map_base, sizeof(req->map_base), true) == false)
				goto ld_0_err;
			
			/* Open the image by name */
			image = 0;
			if (!image) goto ld_0_err;
			if (kp_exe_copy_private(image, req->context, req->context_length) == false)
				goto ld_0_err1;
			
			/* Create handle for this image */
			handle = ke_handle_create(image);
			if (handle == KE_INVALID_HANDLE)
				goto ld_0_err1;

			return handle;
			
		ld_0_err1:
			//TODO: close the object
		ld_0_err:
			return KE_INVALID_HANDLE;
		}
		
		/* Get file base and size */
		case SYSREQ_PROCESS_MAP_EXE_FILE:
		{
			void *base;
		
			module_name = req->name;
		}
		
		/* 删除本地map的文件 */
		case SYSREQ_PROCESS_UNMAP_EXE_FILE:
		{
			void *base = req->name;
		}
		
		/* Add a new exe object, return bool */
		case SYSREQ_PROCESS_ENJECT_EXE:
		{
			struct ko_exe *image;
			void *ctx		= req->context;
			int ctx_size	= req->context_length;
			
			if (ke_validate_user_buffer(ctx, ctx_size, false) == false)
				goto ld_3_err;
			if (ke_validate_user_buffer(module_name, strlen(module_name), false) == false)
				goto ld_3_err;
			if (ctx_size > kp_exe_get_context_size())
				goto ld_3_err;
			if (kp_exe_create_from_file(module_name, ctx) == NULL)
				goto ld_3_err;
			return true;
			
		ld_3_err:
			return false;
		}
		default:
			break;
	}
}

/************************************************************************/
/* MISC                                                                 */
/************************************************************************/

/**
*/
static void kernel_printf(struct sysreq_process_printf * req)
{
	printk("%s", req->string);
}

/************************************************************************/
/* MEMORY                                                               */
/************************************************************************/


/**
	@brief 内核自己的服务
*/
static void * kernel_entry[SYS_REQ_KERNEL_MAX - SYS_REQ_KERNEL_BASE];
static unsigned long kernel_srv(unsigned long req_id, void * req)
{
	unsigned long (*func)(void * req);

	func = kernel_entry[req_id];
	return func(req);
}

/**
	@brief 系统调用总入口
*/
const static struct ke_srv_info ke_srv = {
	.name = "Native 内核服务",
	.service_id_base = SYS_REQ_KERNEL_BASE,
	.request_enqueue = kernel_srv,
};
const static struct  ke_srv_info * ke_interface_v2[KE_SRV_MAX];
asmregparm unsigned long arch_system_call(unsigned long * req)
{
	unsigned long req_id = * req;
	unsigned long ret;
	int clasz = KE_SRV_GET_CLASZ(req_id);
	unsigned long (*func)(unsigned long func_id, void * req);

//	printk("syscall at class %d, req id %d.\n", clasz, KE_SRV_GET_FID(req_id));

	func = ke_interface_v2[clasz]->request_enqueue;
	ret	= ka_call_dynamic_module_entry(func, KE_SRV_GET_FID(req_id), req);

	/* But is a dead thread? */
	//TODO

	return ret;
}

bool ke_srv_register(const struct ke_srv_info * info)
{
	int id = KE_SRV_GET_CLASZ(info->service_id_base);

	/* Set the SRV info pointer, which is the root of the subsystem */
	if (ke_interface_v2[id])
	{
		printk("内核服务 %s 所在的IDBASE已经被注册，无法再次注册。\n", info->name);
		return false;
	}
	ke_interface_v2[id] = info;

	return true;

err1:
	ke_interface_v2[id] = NULL;
	return false;
}

void ke_srv_init()
{
//	kernel_entry[SYS_REQ_KERNEL_THREAD_CREATE - SYS_REQ_KERNEL_BASE]	= (void*) thread_create;
	///kernel_entry[SYS_REQ_KERNEL_THREAD_WAIT - SYS_REQ_KERNEL_BASE]		= (void*) thread_wait;
	//kernel_entry[SYS_REQ_KERNEL_PROCESS_CREATE - SYS_REQ_KERNEL_BASE]	= (void*) process_create;
	kernel_entry[SYS_REQ_KERNEL_PROCESS_STARTUP - SYS_REQ_KERNEL_BASE]		= (void*) process_startup;
	kernel_entry[SYS_REQ_KERNEL_PROCESS_HANDLE_EXE - SYS_REQ_KERNEL_BASE]	= (void*) process_ld;
	//kernel_entry[SYS_REQ_KERNEL_WAIT - SYS_REQ_KERNEL_BASE]				= (void*) process_wait;


	/* Misc */
	kernel_entry[SYS_REQ_KERNEL_PRINTF - SYS_REQ_KERNEL_BASE]			= (void*) kernel_printf;
	//kernel_entry[SYS_REQ_KERNEL_MISC_DRAW_SCREEN - SYS_REQ_KERNEL_BASE] = (void*) misc_draw_screen;

	/* Memory part */
	//kernel_entry[SYS_REQ_KERNEL_VIRTUAL_ALLOC - SYS_REQ_KERNEL_BASE]	= (void*) memory_virtual_alloc;

	ke_srv_register(&ke_srv);
}
