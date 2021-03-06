/* @@@LICENSE
*
*      Copyright (c) 2008-2014 LG Electronics, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */


#include "debug_methods.h"
#include "subscription.h"
#include "base.h"
#include "category.h"

#ifdef MALLOC_DEBUG
#include <malloc.h>
#define __USE_GNU
#include <dlfcn.h>
#endif

#include <string.h>

#ifdef SUBSCRIPTION_DEBUG
bool
_LSPrivateGetSubscriptions(LSHandle* sh, LSMessage *message, void *ctx)
{
    LSError lserror;
    LSErrorInit(&lserror);

    const char *sender = LSMessageGetSenderServiceName(message);

    if ( !sender ||
         ( (strcmp(sender, MONITOR_NAME) != 0) && (strcmp(sender, MONITOR_NAME_PUB) != 0)) )
    {
        LOG_LS_WARNING(MSGID_LS_DEBG_NOT_SUBSCRIBED, 1,
                       PMLOGKS("APP_ID", sender),
                       "Subscription debug method not called by monitor;"
                       " ignoring (service name: %s, unique_name: %s)",
                       sender, LSMessageGetSender(message));
        return true;
    }

    jvalue_ref ret_obj = NULL;
    bool json_ret = _LSSubscriptionGetJson(sh, &ret_obj, &lserror);
    if (!json_ret)
    {
        LOG_LSERROR(MSGID_LS_INVALID_JSON, &lserror);
        LSErrorFree(&lserror);
        return true;
    }

    bool reply_ret = LSMessageReply(sh, message, jvalue_tostring_simple(ret_obj), &lserror);
    if (!reply_ret)
    {
        LOG_LSERROR(MSGID_LS_SUBSEND_FAILED, &lserror);
        LSErrorFree(&lserror);
    }

    j_release(&ret_obj);

    return true;

}
#endif  /* SUBSCRIPTION_DEBUG */

#ifdef MALLOC_DEBUG
bool
_LSPrivateGetMallinfo(LSHandle* sh, LSMessage *message, void *ctx)
{
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref ret_obj = NULL;
    jvalue_ref true_obj = NULL;
    jvalue_ref mallinfo_obj = NULL;
    jvalue_ref allocator_name_obj = NULL;
    jvalue_ref slot_a_obj = NULL;
    jvalue_ref slot_d_obj = NULL;
    jvalue_ref slot_e_obj = NULL;
    jvalue_ref slot_f_obj = NULL;
    jvalue_ref slot_h_obj = NULL;
    jvalue_ref slot_i_obj = NULL;
    jvalue_ref slot_j_obj = NULL;

    const char *sender = LSMessageGetSenderServiceName(message);

    if ( !sender ||
         ( (strcmp(sender, MONITOR_NAME) != 0) && (strcmp(sender, MONITOR_NAME_PUB) != 0)) )
    {
        LOG_LS_WARNING(MSGID_LS_MSG_ERR, 1,
                       PMLOGKS("APP_ID", sender),
                       "Mallinfo debug method not called by monitor;"
                       " ignoring (service name: %s, unique_name: %s)",
                       sender, LSMessageGetSender(message));
        return true;
    }

    ret_obj = jobject_create();
    if (ret_obj == NULL) goto error;

    true_obj = jboolean_create(true);
    if (true_obj == NULL) goto error;

    mallinfo_obj = jobject_create();
    if (mallinfo_obj == NULL) goto error;

    /* returnValue: true,
     * mallinfo: {key: int,...}
     */

    typedef struct mallinfo (*mallinfo_t)();
    static mallinfo_t mallinfo_p = NULL;

    if (mallinfo_p == NULL) {
        mallinfo_p = (mallinfo_t)dlsym(RTLD_DEFAULT, "mallinfo");
        if (mallinfo_p == NULL)
            mallinfo_p = (mallinfo_t)-1;
    }
    struct mallinfo mi;
    if (mallinfo_p != (mallinfo_t)-1) {
        mi = mallinfo_p();
    } else {
        memset(&mi, '\0', sizeof(mi));
    }

    allocator_name_obj = J_CSTR_TO_JVAL("ptmalloc");
    if (allocator_name_obj == NULL) goto error;

    slot_a_obj = jnumber_create_i32(mi.arena);
    if (slot_a_obj == NULL) goto error;

    slot_d_obj = jnumber_create_i32(mi.hblks);
    if (slot_d_obj == NULL) goto error;

    slot_e_obj = jnumber_create_i32(mi.hblkhd);
    if (slot_e_obj == NULL) goto error;

    slot_f_obj = jnumber_create_i32(mi.usmblks);
    if (slot_f_obj == NULL) goto error;

    slot_h_obj = jnumber_create_i32(mi.uordblks);
    if (slot_h_obj == NULL) goto error;

    slot_i_obj = jnumber_create_i32(mi.fordblks);
    if (slot_i_obj == NULL) goto error;

    slot_j_obj = jnumber_create_i32(mi.keepcost);
    if (slot_j_obj == NULL) goto error;

    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("allocator"),
                allocator_name_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("sbrk_bytes"),
                slot_a_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("mmap_count"),
                slot_d_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("mmap_bytes"),
                slot_e_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("max_malloc_bytes"),
                slot_f_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("malloc_bytes"),
                slot_h_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("slack_bytes"),
                slot_i_obj);
    jobject_put(mallinfo_obj,
                J_CSTR_TO_JVAL("trimmable_slack_bytes"),
                slot_j_obj);

    jobject_put(ret_obj,
                J_CSTR_TO_JVAL("returnValue"),
                true_obj);
    jobject_put(ret_obj, J_CSTR_TO_JVAL("mallinfo"),
                mallinfo_obj);

    bool reply_ret = LSMessageReply(sh, message, jvalue_tostring_simple(ret_obj), &lserror);
    if (!reply_ret)
    {
        LOG_LSERROR(MSGID_LS_MALLOC_SEND_FAILED, &lserror);
        LSErrorFree(&lserror);
    }

    j_release(&ret_obj);

    return true;

error:

    j_release(&ret_obj);
    j_release(&true_obj);
    j_release(&mallinfo_obj);

    j_release(&allocator_name_obj);
    j_release(&slot_a_obj);
    j_release(&slot_d_obj);
    j_release(&slot_e_obj);
    j_release(&slot_f_obj);
    j_release(&slot_h_obj);
    j_release(&slot_i_obj);
    j_release(&slot_j_obj);

    return true;
}

bool
_LSPrivateDoMallocTrim(LSHandle* sh, LSMessage *message, void *ctx)
{
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref ret_obj = NULL;
    jvalue_ref true_obj = NULL;
    jvalue_ref malloc_trim_obj = NULL;

    ret_obj = jobject_create();
    if (ret_obj == NULL) goto error;

    true_obj = jboolean_create(true);
    if (true_obj == NULL) goto error;


    /* returnValue: true,
     * malloc_trim: int
     */

    typedef int (*malloc_trim_t)(size_t);
    static malloc_trim_t malloc_trim_p = NULL;

    if (malloc_trim_p == NULL) {
        malloc_trim_p = (malloc_trim_t)dlsym(RTLD_DEFAULT, "malloc_trim");
        if (malloc_trim_p == NULL)
            malloc_trim_p = (malloc_trim_t)-1;
    }

    int result;
    if (malloc_trim_p != (malloc_trim_t)-1) {
        result = malloc_trim_p(0);
    } else {
        result = -1;
    }

    malloc_trim_obj = jnumber_create_i32(result);
    if (malloc_trim_obj == NULL) goto error;

    jobject_put(ret_obj,
                J_CSTR_TO_JVAL("returnValue"),
                true_obj);
    jobject_put(ret_obj,
                J_CSTR_TO_JVAL("malloc_trim"),
                malloc_trim_obj);

    bool reply_ret = LSMessageReply(sh, message, jvalue_tostring_simple(ret_obj), &lserror);
    if (!reply_ret)
    {
        LOG_LSERROR(MSGID_LS_MALLOC_TRIM_SEND_FAILED, &lserror);
        LSErrorFree(&lserror);
    }

    j_release(&ret_obj);

    return true;

error:

    j_release(&ret_obj);
    j_release(&true_obj);
    j_release(&malloc_trim_obj);

    return true;
}
#endif  /* MALLOC_DEBUG */

#ifdef INTROSPECTION_DEBUG
bool
_LSPrivateInrospection(LSHandle* sh, LSMessage *message, void *ctx)
{
    LSError lserror;
    LSErrorInit(&lserror);

    GHashTableIter iter_category, iter_element;
    gpointer name_category, table_category, name_element, callback;
    struct LSCategoryTable *pTable = NULL;

    jvalue_ref ret_obj = NULL;
    jvalue_ref category_obj = NULL;
    jvalue_ref element_obj = NULL;

    ret_obj = jobject_create();

    g_hash_table_iter_init(&iter_category, sh->tableHandlers);
    while (g_hash_table_iter_next(&iter_category, &name_category, &table_category))
    {
        // skip hidden method
        if (strcmp("/com/palm/luna/private", name_category) == 0)
            continue;

        pTable = (struct LSCategoryTable *)table_category;
        category_obj = jobject_create();

        // methods
        g_hash_table_iter_init(&iter_element, pTable->methods);
        while (g_hash_table_iter_next(&iter_element, &name_element, &callback))
        {
            element_obj = J_CSTR_TO_JVAL("METHOD");
            jobject_put(category_obj,
                        jstring_create_copy(j_cstr_to_buffer(name_element)),
                        element_obj);
        }

        // signals
        g_hash_table_iter_init(&iter_element, pTable->signals);
        while (g_hash_table_iter_next(&iter_element, &name_element, &callback))
        {
            element_obj = J_CSTR_TO_JVAL("SIGNAL");
            jobject_put(category_obj,
                        jstring_create_copy(j_cstr_to_buffer(name_element)),
                        element_obj);
        }

        jobject_put(ret_obj,
                    jstring_create_copy(j_cstr_to_buffer(name_category)),
                    category_obj);
    }

    bool reply_ret = LSMessageReply(sh, message, jvalue_tostring_simple(ret_obj), &lserror);
    if (!reply_ret)
    {
        LOG_LSERROR(MSGID_LS_INTROS_SEND_FAILED, &lserror);
        LSErrorFree(&lserror);
        goto error;
    }

    j_release(&ret_obj);

    return true;

error:

    j_release(&ret_obj);
    j_release(&category_obj);
    j_release(&element_obj);

    return false;
}
#endif  /* INTROSPECTION_DEBUG */
