/*
 *
 *
 * Copyright CEA/DAM/DIF  (2011)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    mfsl_async_synclet.c
 * \author  $Author: leibovic $
 * \date    $Date: 2011/05/10 10:24:00 $
 * \version $Revision: 1.0 $
 * \brief   Synclets functions and thread.
 *
 *
 */

#include "config.h"

#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"


#ifndef _USE_SWIG

extern mfsl_parameter_t * mfsl_param;  /* MFSL parameters, from mfsl_async_init.c */
extern unsigned int       end_of_mfsl; /* from mfsl_async_init.c */

pthread_t           * synclet_thread;  /* Synclet Thread Array */
mfsl_synclet_data_t * synclet_data;    /* Synclet Data Array */

extern struct timeval  last_async_window_check;       /* time of last window check, from dispatcher.c */
extern pthread_mutex_t last_async_window_check_mutex; /* mutex associated with previously declared timeval, from dispatcher.c */


/**
 *
 * MFSL_async_process_async_op: processes an asynchronous operation.
 *
 * Processes an asynchronous operation that was taken from a synclet's pending queue
 *
 * @param popdesc [IN]    the asynchronous operation descriptor
 *
 * @return the related fsal_status
 *
 */
fsal_status_t MFSL_async_process_async_op(mfsl_async_op_desc_t * p_async_op_desc)
{
    mfsl_context_t * p_mfsl_context;
    fsal_status_t    fsal_status;
    int              max_tries  = 2;  /* We will retry once if possible */
    int              try_number = 1;  /* First try */
    mfsl_object_t  * current_object;
    int              i;

    SetNameFunction("MFSL_async_process_async_op");

    /* Sanitize */
    if(!p_async_op_desc)
        MFSL_return(ERR_FSAL_FAULT, 0);

    fsal_status.major = 0;
    fsal_status.minor = 0;

    /* Try the operation */
    while(  (  (try_number == 1) || ( FSAL_IS_ERROR(fsal_status) && fsal_is_retryable(fsal_status) )  )
            && (try_number <= max_tries)  )
    {
        /* if it's not the first try, sleep for a while */
        if(try_number > 1)
            sleep(1); /** \todo make this a parameter */

        LogDebug(COMPONENT_MFSL, "Processing operation (%u, %s). Try #%d.",
                p_async_op_desc->op_type,
                mfsl_async_op_name[p_async_op_desc->op_type],
                try_number);

        /* Call operation on data */
        fsal_status = (p_async_op_desc->op_func) (p_async_op_desc);

        /* Don't forget to increment... */
        try_number += 1;
    }

    /** @todo: check the results with computed ones.
     *  update the cache_inode if different (needs some way to call cache_inode)
     * */

    if(FSAL_IS_ERROR(fsal_status))
    {
        /* Hopefully this should never happen. */
        LRU_entry_t    * failed_op_entry; /* will contain a failed_op */
        LRU_status_t     lru_status;

        LogMajor(COMPONENT_MFSL, "Impossible to process: op_type=%u %s : error (%u, %u)",
                p_async_op_desc->op_type, mfsl_async_op_name[p_async_op_desc->op_type],
                fsal_status.major, fsal_status.minor);

        /* Post this operation in a dedicated LRU, for debug. */
        P(synclet_data[p_async_op_desc->related_synclet_index].mutex_failed_op_lru);

        failed_op_entry = LRU_new_entry(synclet_data[p_async_op_desc->related_synclet_index].failed_op_lru, &lru_status);
        if(!failed_op_entry)
        {
            LogCrit(COMPONENT_MFSL, "Impossible to get an entry in failed_op_lru for synclet %d.",
                    p_async_op_desc->related_synclet_index);
        }

        failed_op_entry->buffdata.pdata = (caddr_t) p_async_op_desc;
        failed_op_entry->buffdata.len   = sizeof(mfsl_async_op_desc_t);
        failed_op_entry->valid_state    = LRU_ENTRY_VALID;

        V(synclet_data[p_async_op_desc->related_synclet_index].mutex_failed_op_lru);

        /* Return the status */
        MFSL_return(fsal_status.major, fsal_status.minor);
    }

    /* Everything's OK. */
    LogDebug(COMPONENT_MFSL, "Operation (%u, %s) processed",
            p_async_op_desc->op_type,
            mfsl_async_op_name[p_async_op_desc->op_type]);

    /* Is this the last operation on this data? */
    for(i=0; p_async_op_desc->concerned_objects[i] != NULL; i++)
    {
        current_object = p_async_op_desc->concerned_objects[i];
        P(last_async_window_check_mutex);
        if((current_object->p_last_op_desc == p_async_op_desc) ||
            timercmp(&current_object->last_op_time, &last_async_window_check, <))
        {
            current_object->p_last_op_desc = NULL;
        }
        V(last_async_window_check_mutex);
    }

    /* Free the previously allocated structures */
    p_mfsl_context = (mfsl_context_t *) p_async_op_desc->ptr_mfsl_context;

    P(p_mfsl_context->lock);
    ReleaseToPool(p_async_op_desc, &p_mfsl_context->pool_async_op);
    V(p_mfsl_context->lock);

    MFSL_return(ERR_FSAL_NO_ERROR, 0);
}

/**
 *
 * MFSL_async_choose_synclet: Choose the synclet that will receive an asynchornous op to manage
 *
 * @param candidate_async_op    the asynchronous operation we want to perform
 *
 * @return the index for the synclet to be used.
 *
 */
unsigned int MFSL_async_choose_synclet(mfsl_async_op_desc_t * candidate_async_op)
{
    SetNameFunction("MFSL_async_choose_synclet");
    /** @todo: implement this */

    /* For the moment we only have one synclet running.
     * Return 0 anyway.
     * */
    return 0;
}


/**
 * mfsl_async_synclet_thread: thread used for asynchronous operation management.
 *
 * This thread is used for managing asynchronous operation management
 *
 * @param arg   the index for the thread
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void * mfsl_async_synclet_thread(void * arg)
{
    mfsl_synclet_data_t  * my_synclet_data = (mfsl_synclet_data_t *) arg;
    mfsl_async_op_desc_t * current_async_operation;
    LRU_entry_t          * current_lru_entry;
    fsal_status_t          fsal_status;
    char                   namestr[64]; /* name of the function */
    int                    rc=0;        /* Used by BuddyInit */
    int                    i;
    LRU_status_t           lru_status;

    sprintf(namestr, "mfsl_async_synclet_thread #%d", my_synclet_data->index);
    SetNameFunction(namestr);

    /*************************
     *     Initialisation
     *************************/
#ifndef _NO_BUDDY_SYSTEM
    /* Initialize Memory */
    if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
        /* Failed init */
        LogCrit(COMPONENT_MFSL, "Memory manager could not be initialized, exiting...");
        exit(1);
    }
    LogEvent(COMPONENT_MFSL, "Memory manager successfully initialized.");
#endif

    LogEvent(COMPONENT_MFSL, "Synclets initialisation.");

    /* Data initialisation
     *********************/
    LogDebug(COMPONENT_MFSL, "Initialisation of synclet number %d.", my_synclet_data->index);

    if(pthread_cond_init(&synclet_data[my_synclet_data->index].op_condvar, NULL)        != 0)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to initialize op_condvar for synclet %d.", my_synclet_data->index);
        exit(1);
    }

    if(pthread_mutex_init(&synclet_data[my_synclet_data->index].mutex_op_condvar, NULL) != 0)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to initialize mutex_op_condvar for synclet %d.", my_synclet_data->index);
        exit(1);
    }

    if(pthread_mutex_init(&synclet_data[my_synclet_data->index].mutex_op_lru, NULL)     != 0)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to initialize mutex_op_lru for synclet %d.", my_synclet_data->index);
        exit(1);
    }

    if((synclet_data[my_synclet_data->index].op_lru = LRU_Init(mfsl_param->lru_param, &lru_status)) == NULL)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to initialize op_lru for synclet %d. lru_status: %d",
                my_synclet_data->index, (int) lru_status);
        exit(1);
    }

    if(pthread_mutex_init(&synclet_data[my_synclet_data->index].mutex_failed_op_lru, NULL)     != 0)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to initialize mutex_failed_op_lru for synclet %d.", my_synclet_data->index);
        exit(1);
    }

    if((synclet_data[my_synclet_data->index].failed_op_lru = LRU_Init(mfsl_param->lru_param, &lru_status)) == NULL)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to initialize failed_op_lru for synclet %d. lru_status: %d",
                my_synclet_data->index, (int) lru_status);
        exit(1);
    }

    synclet_data[my_synclet_data->index].passcounter = 0;


    /*************************
     *         Work
     *************************/
    LogEvent(COMPONENT_MFSL, "Synclet %d starts.", my_synclet_data->index);

    /* Synclet loop. */
    while(!end_of_mfsl)
    {
        /* Wait for a signal from dispatcher_thread */
        P(my_synclet_data->mutex_op_condvar);
        while(my_synclet_data->op_lru->nb_entry == my_synclet_data->op_lru->nb_invalid)
            pthread_cond_wait(&my_synclet_data->op_condvar, &my_synclet_data->mutex_op_condvar);
        V(my_synclet_data->mutex_op_condvar);

        /* Loop on my_synclet_data.op_lru */
        P(my_synclet_data->mutex_op_lru);
        for(current_lru_entry=my_synclet_data->op_lru->LRU ; current_lru_entry != NULL ;
                current_lru_entry=current_lru_entry->next)
        {
            /* ignore invalid entries */
            if(current_lru_entry->valid_state == LRU_ENTRY_VALID)
            {
                /* Get current asynchronous operation to perform. */
                current_async_operation = (mfsl_async_op_desc_t *) (current_lru_entry->buffdata.pdata);

                LogDebug(COMPONENT_MFSL, "Found an operation to process: %p.", (caddr_t) current_async_operation);

                fsal_status = MFSL_async_process_async_op(current_async_operation);

                if(FSAL_IS_ERROR(fsal_status)) /* @todo: should never happen, unless we change the process function */
                    LogCrit(COMPONENT_MFSL, "Impossible to perform asynchronous operation.");

                /* Invalidate entry. */
                if(LRU_invalidate(my_synclet_data->op_lru, current_lru_entry) != LRU_LIST_SUCCESS)
                    LogCrit(COMPONENT_MFSL, "Impossible to invalidate lru_entry in synclet_data %d.", my_synclet_data->index);
            }
        }

        /* We just made another loop. */
        my_synclet_data->passcounter += 1;

        /* Garbage collect */
        if(my_synclet_data->passcounter >= mfsl_param->nb_before_gc)
        {
            if(LRU_gc_invalid(my_synclet_data->op_lru, NULL) != LRU_LIST_SUCCESS)
                LogCrit(COMPONENT_MFSL, "Impossible to garbage collect op_lru in synclet %d. passcounter: %d.", my_synclet_data->index, my_synclet_data->passcounter);

            my_synclet_data->passcounter = 0;
        }

        V(my_synclet_data->mutex_op_lru);
    } /* end_of_mfsl */

    LogEvent(COMPONENT_MFSL, "Synclet %d Ends.", my_synclet_data->index);

    return NULL;
}


/**
 *
 * MFSL_async_synclet_init: initializes dispatcher threads.
 *
 * Initializes synclet threads
 *
 * @param *arg   Not used
 *
 */
fsal_status_t MFSL_async_synclet_init(void * arg)
{
    pthread_attr_t synclet_thread_attr;
    int            i=0;
    int            rc=0;

    SetNameFunction("MFSL_async_synclet_init");

    LogEvent(COMPONENT_MFSL, "Synclets creation.");

    pthread_attr_init(&synclet_thread_attr);
    pthread_attr_setscope(&synclet_thread_attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&synclet_thread_attr, PTHREAD_CREATE_JOINABLE);

    /* Thread Array */
    if((synclet_thread = (pthread_t *)
                Mem_Alloc(mfsl_param->nb_synclet * sizeof(pthread_t))) == NULL)
        MFSL_return(ERR_FSAL_NOMEM, errno);

    /* Data Array */
    if((synclet_data = (mfsl_synclet_data_t *)
                Mem_Alloc(mfsl_param->nb_synclet * sizeof(mfsl_synclet_data_t))) == NULL)
    {
        LogCrit(COMPONENT_MFSL, "Impossible to allocate synclet_data. errno: %d.", errno);
        exit(1);
    }

    for(i=0; i < mfsl_param->nb_synclet; i++)
    {
        LogDebug(COMPONENT_MFSL, "Creation of synclet number %d.", i);

        synclet_data[i].index = i;

        if((rc = pthread_create(&synclet_thread[i],
                        &synclet_thread_attr,
                        mfsl_async_synclet_thread,
                        (void *) &synclet_data[i]))     != 0)
            MFSL_return(ERR_FSAL_SERVERFAULT, -rc);
    }

    MFSL_return(ERR_FSAL_NO_ERROR, 0);
}

#endif /* _USE_SWIG */
