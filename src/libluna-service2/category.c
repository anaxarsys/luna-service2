/****************************************************************
 * @@@LICENSE
 *
 * Copyright (c) 2014 LG Electronics, Inc.
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
 * LICENSE@@@
 ****************************************************************/

/**
 *  @file category.c
 */

#include "category.h"
#include "lserror_pbnjson.h"
#include "simple_pbnjson.h"

#include "luna-service2/lunaservice.h"
#include "luna-service2/lunaservice-meta.h"
#include "log.h"

/**
 * @addtogroup LunaServiceInternals
 * @{
 */

static void
_LSCategoryTableFree(LSCategoryTable *table)
{
    if (table->methods)
        g_hash_table_unref(table->methods);
    if (table->signals)
        g_hash_table_unref(table->signals);
    if (table->properties)
        g_hash_table_unref(table->properties);

    j_release(&table->description);

#ifdef MEMCHECK
    memset(table, 0xFF, sizeof(LSCategoryTable));
#endif

    g_free(table);
}

static char*
_category_to_object_path_alloc(const char *category)
{
    char *category_path;

    if (NULL == category)
    {
        category_path = g_strdup("/"); // default category
    }
    else if ('/' == category[0])
    {
        category_path = g_strdup(category);
    }
    else
    {
        category_path = g_strdup_printf("/%s", category);
    }

    return category_path;
}

static bool
_category_exists(LSHandle *sh, const char *category)
{
    if (!sh->tableHandlers) return false;

    char *category_path = _category_to_object_path_alloc(category);
    bool exists = false;

    if (g_hash_table_lookup(sh->tableHandlers, category_path))
    {
        exists = true;
    }

    g_free(category_path);

    return exists;
}

static LSCategoryTable *
LSHandleGetCategory(LSHandle *sh, const char *category, LSError *error)
{
    LSCategoryTable *table;
    char *categoryPath = _category_to_object_path_alloc(category);

    _LSErrorGotoIfFail(fail, sh->tableHandlers != NULL, error, MSGID_LS_NO_CATEGORY_TABLE,
        -1, "%s: %s not registered.", __FUNCTION__, category);

    table = g_hash_table_lookup(sh->tableHandlers, categoryPath);
    _LSErrorGotoIfFail(fail, table != NULL, error, MSGID_LS_NO_CATEGORY,
        -1, "%s: %s not registered.", __FUNCTION__, category);

    g_free(categoryPath);
    return table;

fail:
    g_free(categoryPath);

    return NULL;
}

static LSMethodEntry *LSMethodEntryCreate()
{
    LSMethodEntry *entry = g_slice_new0(LSMethodEntry);
    return entry;
}

static void LSMethodEntrySet(LSMethodEntry *entry, LSMethod *method)
{
    LS_ASSERT(method);
    LS_ASSERT(method->function);

    entry->function = method->function;
    entry->flags = method->flags;

    /* clean out call schema if no validation needed */
    if (!(entry->flags & LUNA_METHOD_FLAG_VALIDATE_IN))
    { jschema_release(&entry->schema_call); }
}

static void LSMethodEntryFree(void *methodEntry)
{
    LS_ASSERT(methodEntry);

    LSMethodEntry *entry = methodEntry;

    /* clean out schemas if any present */
    jschema_release(&entry->schema_call);
    jschema_release(&entry->schema_reply);
    jschema_release(&entry->schema_firstReply);

    g_slice_free(LSMethodEntry, methodEntry);
}

static jvalue_ref jvalue_shallow(jvalue_ref value)
{
    if (jis_array(value))
    {
        jvalue_ref array = jarray_create_hint(NULL, jarray_size(value));
        jarray_splice_append(array, value, SPLICE_COPY);
        return array;
    }
    else if (jis_object(value))
    {
        jobject_iter iter;
        if (!jobject_iter_init(&iter, value))
        { return jinvalid(); }

        jvalue_ref object = jobject_create();

        jobject_key_value keyval;
        while (jobject_iter_next(&iter, &keyval))
        {
            jobject_set2(object, keyval.key, keyval.value);
        }
        return object;
    }
    else
    { return jvalue_duplicate(value); }
}

/* unfortunately J_CSTR_TO_JVAL(xxx) is not a constant */
#define KEYWORD_DEFINITIONS J_CSTR_TO_JVAL("definitions")
#define KEYWORD_METHODS J_CSTR_TO_JVAL("methods")

static jschema_ref prepare_schema(jvalue_ref schema_value, jvalue_ref defs)
{
    LS_ASSERT(schema_value != NULL);
    LS_ASSERT(jis_object(schema_value));
    LS_ASSERT(defs == NULL || jis_object(defs));

    if (defs == NULL) /* simple case without mixing in defs */
    {
        return jschema_parse_jvalue(schema_value, NULL, "");
    }

    /* mix together two definitions into one (local scope overrides) */
    jvalue_ref orig_defs, mixed_defs = NULL;
    if (jobject_get_exists2(schema_value, KEYWORD_DEFINITIONS, &orig_defs))
    {
        jobject_iter iter;
        if (jobject_iter_init(&iter, defs))
        {
            mixed_defs = jvalue_shallow(orig_defs);
            jobject_key_value keyval;
            while (jobject_iter_next(&iter, &keyval))
            {
                if (jobject_get_exists2(orig_defs, keyval.key, NULL)) continue;
                jobject_set2(mixed_defs, keyval.key, keyval.value);
            }
        }
    }

    LS_ASSERT(orig_defs == NULL || mixed_defs != NULL);

    /* mix defs into original schema */
    jvalue_ref mixed_schema_value = jvalue_shallow(schema_value);
    if (mixed_defs != NULL)
    {
        jobject_put(mixed_schema_value, KEYWORD_DEFINITIONS, mixed_defs);
    }
    else if (orig_defs == NULL)
    {
        jobject_set2(mixed_schema_value, KEYWORD_DEFINITIONS, defs);
    }

    jschema_ref schema = jschema_parse_jvalue(schema_value, NULL, "");
    j_release(&mixed_schema_value);

    return schema;
}

static jschema_ref default_reply_schema()
{
    const char *schema_text = "{\"oneOf\":["
        "{\"type\":\"object\",\"properties\":{"
            "\"returnValue\":{\"enum\":[true]}"
        "}, \"required\":[\"returnValue\"] },"
        "{\"type\":\"object\",\"properties\":{"
            "\"returnValue\":{\"enum\":[false]},"
            "\"errorCode\":{\"type\":\"integer\"},"
            "\"errorText\":{\"type\":\"string\"}"
        "}, \"required\":[\"returnValue\"] }"
    "]}";

    static jschema_ref schema = NULL;
    /* XXX: thread-safe??? */
    if (schema == NULL)
    {
        schema = jschema_parse(j_cstr_to_buffer(schema_text), JSCHEMA_DOM_NOOPT, NULL);
    }
    LS_ASSERT( schema != NULL );
    return schema;
}

bool LSCategoryValidateCall(LSMethodEntry *entry, LSMessage *message)
{
    const char *badReplyFallback = "{\"returnValue\":false,"
                                    "\"errorText\":\"non-representable validation error\"}";

    LS_ASSERT( entry->schema_call ); /* this is a bug if service didn't supplied a schema */

    jvalue_ref reply = NULL;
    const char *payload;
    LSError error;
    LSErrorInit(&error);
    if (entry->schema_call == NULL)
    {
        payload = "{\"returnValue\":false,"
                   "\"errorText\":\"service didn't provided schema, but expects validation\"}";
    }
    else
    {
        struct JErrorCallbacks errorCallbacks;
        JSchemaInfo schemaInfo;

        SetLSErrorCallbacks(&errorCallbacks, &error);

        jschema_info_init(&schemaInfo, entry->schema_call, NULL, &errorCallbacks);

        jvalue_ref dom = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

        if (jis_valid(dom)) /* no error - nothing to do */
        { return true; }

        j_release(&dom); /* TODO: save in LSMessage for callback */

        reply = jobject_create_var(
            jkeyval( J_CSTR_TO_JVAL("returnValue"), jboolean_create(false) ),
            jkeyval( J_CSTR_TO_JVAL("errorText"), jstring_create(error.message) ),
            J_END_OBJ_DECL
        );

        payload = jis_valid(reply) ? jvalue_tostring(reply, jschema_all()) : NULL;
        if (payload == NULL) payload = badReplyFallback;
    }

    LOG_LS_ERROR("INVALID_CALL", 4,
        PMLOGKS("SENDER", LSMessageGetSenderServiceName(message)),
        PMLOGKS("CATEGORY", LSMessageGetCategory(message)),
        PMLOGKS("METHOD", LSMessageGetMethod(message)),
        PMLOGJSON("ERROR", payload),
        "Validation failed for request %s", LSMessageGetPayload(message)
    );

    LSErrorFree(&error);

    if (!LSMessageRespond(message, payload, &error))
    {
        LSErrorLog(PmLogGetLibContext(), "INVALID_CALL_RESPOND", &error);
        LSErrorFree(&error);
    }

    j_release(&reply);

    return false;
}

/* @} END OF LunaServiceInternals */

/**
* @brief Append methods to the category.
*        Creates a category if needed.
*
* @param  sh
* @param  category
* @param  methods
* @param  signals
* @param  category_user_data
* @param  lserror
*
* @retval
*/
bool
LSRegisterCategoryAppend(LSHandle *sh, const char *category,
                   LSMethod      *methods,
                   LSSignal      *signals,
                   LSError *lserror)
{
    LSHANDLE_VALIDATE(sh);

    LSCategoryTable *table = NULL;

    if (!sh->tableHandlers)
    {
        sh->tableHandlers = g_hash_table_new_full(g_str_hash, g_str_equal,
            /*key*/ (GDestroyNotify)g_free,
            /*value*/ (GDestroyNotify)_LSCategoryTableFree);
    }

    char *category_path = _category_to_object_path_alloc(category);

    table =  g_hash_table_lookup(sh->tableHandlers, category_path);
    if (!table)
    {
        table = g_new0(LSCategoryTable, 1);

        table->sh = sh;
        table->methods    = g_hash_table_new_full(g_str_hash, g_str_equal, free, LSMethodEntryFree);
        table->signals    = g_hash_table_new(g_str_hash, g_str_equal);
        table->category_user_data = NULL;
        table->description = NULL;

        g_hash_table_replace(sh->tableHandlers, category_path, table);

    }
    else
    {
        /*
         * We've already registered the category, so free the unneeded
         * category_path. This will happen when we call
         * LSRegisterCategoryAppend multiple times with the same category
         * (i.e., LSPalmServiceRegisterCategory)
         */
        g_free(category_path);
        category_path = NULL;
    }

    /* Add methods to table. */

    if (methods)
    {
        LSMethod *m;
        for (m = methods; m->name && m->function; m++)
        {
            LSMethodEntry *entry = g_hash_table_lookup(table->methods, m->name);
            if (entry == NULL)
            {
                entry = LSMethodEntryCreate();
                g_hash_table_insert(table->methods, strdup(m->name), entry);
            }
            LSMethodEntrySet(entry, m);
        }
    }

    if (signals)
    {
        LSSignal *s;
        for (s = signals; s->name; s++)
        {
            g_hash_table_replace(table->signals, (gpointer)s->name, s);
        }
    }

    if (sh->name)
    {
        // Unlikely
        if (!_LSTransportAppendCategory(sh->transport, category, methods, lserror))
        {
            LOG_LS_ERROR(MSGID_LS_CONN_ERROR, 0, "Failed to notify the hub about category append.");
            return false;
        }
    }

    return true;
}

/**
* @brief Register public methods and private methods.
*
* @param  psh
* @param  category
* @param  methods_public
* @param  methods_private
* @param  signals
* @param  category_user_data
* @param  lserror
*
* @retval
*/
bool
LSPalmServiceRegisterCategory(LSPalmService *psh,
    const char *category, LSMethod *methods_public, LSMethod *methods_private,
    LSSignal *signals, void *category_user_data, LSError *lserror)
{
    bool retVal;

    retVal = LSRegisterCategoryAppend(psh->public_sh,
        category, methods_public, signals, lserror);
    if (!retVal) goto error;

    retVal = LSCategorySetData(psh->public_sh, category,
                      category_user_data, lserror);
    if (!retVal) goto error;

    /* Private bus is union of public and private methods. */

    retVal = LSRegisterCategoryAppend(psh->private_sh,
        category, methods_private, signals, lserror);
    if (!retVal) goto error;

    retVal = LSRegisterCategoryAppend(psh->private_sh,
        category, methods_public, NULL, lserror);
    if (!retVal) goto error;

    retVal = LSCategorySetData(psh->private_sh, category,
                      category_user_data, lserror);
    if (!retVal) goto error;
error:
    return retVal;
}

/**
* @brief Set the userdata that is delivered to each callback registered
*        to the category.
*
* @param  sh
* @param  category
* @param  user_data
* @param  lserror
*
* @retval
*/
bool
LSCategorySetData(LSHandle *sh, const char *category, void *user_data, LSError *lserror)
{
    LSHANDLE_VALIDATE(sh);

    LSCategoryTable *table = LSHandleGetCategory(sh, category, lserror);
    if (table == NULL) return false;

    table->category_user_data = user_data;

    return true;
}

bool LSCategorySetDescription(
    LSHandle *sh, const char *category,
    jvalue_ref description,
    LSError *error
)
{
    LSHANDLE_VALIDATE(sh);

    LSCategoryTable *table = LSHandleGetCategory(sh, category, error);
    if (table == NULL) return false;

    /* TODO: use validation once it will be able to fill defaults */

    jvalue_ref defs;
    if (!jobject_get_exists2(description, KEYWORD_DEFINITIONS, &defs)) defs = NULL;

    jvalue_ref methods;
    if (!jobject_get_exists2(description, KEYWORD_METHODS, &methods)) methods = NULL;

    LS_ASSERT( methods != NULL );

    jobject_iter iter;
    if (methods == NULL || !jobject_iter_init(&iter, methods))
    {
        _LSErrorSetNoPrint(error, -1, "Category description should have "
                                      "property \"methods\" with an object as a value");
        return false;
    }

    jobject_key_value keyval;
    while (jobject_iter_next(&iter, &keyval))
    {
        LOCAL_CSTR_FROM_BUF(method_name, jstring_get_fast(keyval.key));

        LSMethodEntry *entry = g_hash_table_lookup(table->methods, method_name);
        if (entry == NULL)
        {
            /* create a stub entry for further filling with appropriate callback */
            entry = LSMethodEntryCreate();

            /* build and keep schema in case if this flag will be true */
            entry->flags |= LUNA_METHOD_FLAG_VALIDATE_IN;

            g_hash_table_insert(table->methods, strdup(method_name), entry);
        }
        else
        {
            /* clean out old schemas if any present */
            jschema_release(&entry->schema_call);
            jschema_release(&entry->schema_reply);
            jschema_release(&entry->schema_firstReply);
        }

        jvalue_ref value;

        if (entry->flags & LUNA_METHOD_FLAG_VALIDATE_IN)
        {
            if (jobject_get_exists(keyval.value, J_CSTR_TO_BUF("call"), &value))
            { entry->schema_call = prepare_schema(value, defs); }
            else
            { entry->schema_call = jschema_all(); }
        }

        /* TODO: introduce global switch that turns on replies validation */
        if (jobject_get_exists(keyval.value, J_CSTR_TO_BUF("reply"), &value))
        { entry->schema_reply = prepare_schema(value, defs); }
        else
        { entry->schema_reply = jschema_copy(default_reply_schema()); }

        if (jobject_get_exists(keyval.value, J_CSTR_TO_BUF("firstReply"), &value))
        { entry->schema_firstReply = prepare_schema(value, defs); }
        else
        { entry->schema_firstReply = jschema_copy(entry->schema_reply); }
    }

    j_release(&table->description);
    table->description = jvalue_copy(description);

    return true;
}

/**
* @brief Register tables of callbacks associated with the message category.
*
* @param  category    - May be NULL for default '/' category.
* @param  methods     - table of methods.
* @param  signals     - table of signals.
* @param  properties  - table of properties.
* @param  lserror
*
* @retval
*/
bool
LSRegisterCategory(LSHandle *sh, const char *category,
                   LSMethod      *methods,
                   LSSignal      *signals,
                   LSProperty    *properties, LSError *lserror)
{
    _LSErrorIfFail(sh != NULL, lserror, MSGID_LS_INVALID_HANDLE);

    LSHANDLE_VALIDATE(sh);

    if (_category_exists(sh, category))
    {
        _LSErrorSet(lserror, MSGID_LS_CATEGORY_REGISTERED, -1,
                    "Category %s already registered.", category);
        return false;
    }

    return LSRegisterCategoryAppend(sh, category, methods, signals, lserror);
}
