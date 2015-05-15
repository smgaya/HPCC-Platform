/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2013 HPCC Systems.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

#include "platform.h"
#include "cassandra.h"
#include "jexcept.hpp"
#include "jthread.hpp"
#include "hqlplugins.hpp"
#include "deftype.hpp"
#include "eclhelper.hpp"
#include "eclrtl.hpp"
#include "eclrtl_imp.hpp"
#include "rtlds_imp.hpp"
#include "rtlfield_imp.hpp"
#include "rtlembed.hpp"
#include "roxiemem.hpp"
#include "nbcd.hpp"
#include "jptree.hpp"

#include "workunit.hpp"
#include "workunit.ipp"


#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif


static void UNSUPPORTED(const char *feature) __attribute__((noreturn));

static void UNSUPPORTED(const char *feature)
{
    throw MakeStringException(-1, "UNSUPPORTED feature: %s not supported in Cassandra plugin", feature);
}

static const char * compatibleVersions[] = {
    "Cassandra Embed Helper 1.0.0",
    NULL };

static const char *version = "Cassandra Embed Helper 1.0.0";

extern "C" EXPORT bool getECLPluginDefinition(ECLPluginDefinitionBlock *pb)
{
    if (pb->size == sizeof(ECLPluginDefinitionBlockEx))
    {
        ECLPluginDefinitionBlockEx * pbx = (ECLPluginDefinitionBlockEx *) pb;
        pbx->compatibleVersions = compatibleVersions;
    }
    else if (pb->size != sizeof(ECLPluginDefinitionBlock))
        return false;
    pb->magicVersion = PLUGIN_VERSION;
    pb->version = version;
    pb->moduleName = "cassandra";
    pb->ECL = NULL;
    pb->flags = PLUGIN_MULTIPLE_VERSIONS;
    pb->description = "Cassandra Embed Helper";
    return true;
}

namespace cassandraembed {

static void failx(const char *msg, ...) __attribute__((noreturn))  __attribute__((format(printf, 1, 2)));
static void fail(const char *msg) __attribute__((noreturn));

static void failx(const char *message, ...)
{
    va_list args;
    va_start(args,message);
    StringBuffer msg;
    msg.append("cassandra: ").valist_appendf(message,args);
    va_end(args);
    rtlFail(0, msg.str());
}

static void fail(const char *message)
{
    StringBuffer msg;
    msg.append("cassandra: ").append(message);
    rtlFail(0, msg.str());
}

// Wrappers to Cassandra structures that require corresponding releases

class CassandraCluster : public CInterface
{
public:
    CassandraCluster(CassCluster *_cluster) : cluster(_cluster), batchMode((CassBatchType) -1)
    {
    }
    void setOptions(const StringArray &options)
    {
        const char *contact_points = "localhost";
        const char *user = "";
        const char *password = "";
        ForEachItemIn(idx, options)
        {
            const char *opt = options.item(idx);
            const char *val = strchr(opt, '=');
            if (val)
            {
                StringBuffer optName(val-opt, opt);
                val++;
                if (stricmp(optName, "contact_points")==0 || stricmp(optName, "server")==0)
                    contact_points = val;   // Note that lifetime of val is adequate for this to be safe
                else if (stricmp(optName, "user")==0)
                    user = val;
                else if (stricmp(optName, "password")==0)
                    password = val;
                else if (stricmp(optName, "keyspace")==0)
                    keyspace.set(val);
                else if (stricmp(optName, "batch")==0)
                {
                    if (stricmp(val, "LOGGED")==0)
                        batchMode = CASS_BATCH_TYPE_LOGGED;
                    else if (stricmp(val, "UNLOGGED")==0)
                        batchMode = CASS_BATCH_TYPE_UNLOGGED;
                    else if (stricmp(val, "COUNTER")==0)
                        batchMode = CASS_BATCH_TYPE_COUNTER;
                }
                else if (stricmp(optName, "port")==0)
                {
                    unsigned port = getUnsignedOption(val, "port");
                    checkSetOption(cass_cluster_set_port(cluster, port), "port");
                }
                else if (stricmp(optName, "protocol_version")==0)
                {
                    unsigned protocol_version = getUnsignedOption(val, "protocol_version");
                    checkSetOption(cass_cluster_set_protocol_version(cluster, protocol_version), "protocol_version");
                }
                else if (stricmp(optName, "num_threads_io")==0)
                {
                    unsigned num_threads_io = getUnsignedOption(val, "num_threads_io");
                    cass_cluster_set_num_threads_io(cluster, num_threads_io);  // No status return
                }
                else if (stricmp(optName, "queue_size_io")==0)
                {
                    unsigned queue_size_io = getUnsignedOption(val, "queue_size_io");
                    checkSetOption(cass_cluster_set_queue_size_io(cluster, queue_size_io), "queue_size_io");
                }
                else if (stricmp(optName, "core_connections_per_host")==0)
                {
                    unsigned core_connections_per_host = getUnsignedOption(val, "core_connections_per_host");
                    checkSetOption(cass_cluster_set_core_connections_per_host(cluster, core_connections_per_host), "core_connections_per_host");
                }
                else if (stricmp(optName, "max_connections_per_host")==0)
                {
                    unsigned max_connections_per_host = getUnsignedOption(val, "max_connections_per_host");
                    checkSetOption(cass_cluster_set_max_connections_per_host(cluster, max_connections_per_host), "max_connections_per_host");
                }
                else if (stricmp(optName, "max_concurrent_creation")==0)
                {
                    unsigned max_concurrent_creation = getUnsignedOption(val, "max_concurrent_creation");
                    checkSetOption(cass_cluster_set_max_concurrent_creation(cluster, max_concurrent_creation), "max_concurrent_creation");
                }
                else if (stricmp(optName, "pending_requests_high_water_mark")==0)
                {
                    unsigned pending_requests_high_water_mark = getUnsignedOption(val, "pending_requests_high_water_mark");
                    checkSetOption(cass_cluster_set_pending_requests_high_water_mark(cluster, pending_requests_high_water_mark), "pending_requests_high_water_mark");
                }
                else if (stricmp(optName, "pending_requests_low_water_mark")==0)
                {
                    unsigned pending_requests_low_water_mark = getUnsignedOption(val, "pending_requests_low_water_mark");
                    checkSetOption(cass_cluster_set_pending_requests_low_water_mark(cluster, pending_requests_low_water_mark), "pending_requests_low_water_mark");
                }
                else if (stricmp(optName, "max_concurrent_requests_threshold")==0)
                {
                    unsigned max_concurrent_requests_threshold = getUnsignedOption(val, "max_concurrent_requests_threshold");
                    checkSetOption(cass_cluster_set_max_concurrent_requests_threshold(cluster, max_concurrent_requests_threshold), "max_concurrent_requests_threshold");
                }
                else if (stricmp(optName, "connect_timeout")==0)
                {
                    unsigned connect_timeout = getUnsignedOption(val, "connect_timeout");
                    cass_cluster_set_connect_timeout(cluster, connect_timeout);
                }
                else if (stricmp(optName, "request_timeout")==0)
                {
                    unsigned request_timeout = getUnsignedOption(val, "request_timeout");
                    cass_cluster_set_request_timeout(cluster, request_timeout);
                }
                else
                    failx("Unrecognized option %s", optName.str());
            }
        }
        cass_cluster_set_contact_points(cluster, contact_points);
        if (*user || *password)
            cass_cluster_set_credentials(cluster, user, password);
    }
    ~CassandraCluster()
    {
        if (cluster)
            cass_cluster_free(cluster);
    }
    inline operator CassCluster *() const
    {
        return cluster;
    }
private:
    void checkSetOption(CassError rc, const char *name)
    {
        if (rc != CASS_OK)
        {
            failx("While setting option %s: %s", name, cass_error_desc(rc));
        }
    }
    unsigned getUnsignedOption(const char *val, const char *option)
    {
        char *endp;
        long value = strtoul(val, &endp, 0);
        if (endp==val || *endp != '\0' || value > INT_MAX || value < INT_MIN)
            failx("Invalid value '%s' for option %s", val, option);
        return (int) value;
    }
    CassandraCluster(const CassandraCluster &);
    CassCluster *cluster;
public:
    // These are here as convenient to set from same options string. They are really properties of the session
    // rather than the cluster, but we have one session per cluster so we get away with it.
    CassBatchType batchMode;
    StringAttr keyspace;
};

class CassandraFuture : public CInterface
{
public:
    CassandraFuture(CassFuture *_future) : future(_future)
    {
    }
    ~CassandraFuture()
    {
        if (future)
            cass_future_free(future);
    }
    inline operator CassFuture *() const
    {
        return future;
    }
    void wait(const char *why)
    {
        cass_future_wait(future);
        CassError rc = cass_future_error_code(future);
        if(rc != CASS_OK)
        {
            CassString message = cass_future_error_message(future);
            VStringBuffer err("cassandra: failed to %s (%.*s)", why, (int)message.length, message.data);
            rtlFail(0, err.str());
        }
    }
    void set(CassFuture *_future)
    {
        if (future)
            cass_future_free(future);
        future = _future;
    }
private:
    CassandraFuture(const CassandraFuture &);
    CassFuture *future;
};

class CassandraSession : public CInterface
{
public:
    CassandraSession() : session(NULL) {}
    CassandraSession(CassSession *_session) : session(_session)
    {
    }
    ~CassandraSession()
    {
        set(NULL);
    }
    void set(CassSession *_session)
    {
        if (session)
        {
            CassandraFuture close_future(cass_session_close(session));
            cass_future_wait(close_future);
            cass_session_free(session);
        }
        session = _session;
    }
    inline operator CassSession *() const
    {
        return session;
    }
private:
    CassandraSession(const CassandraSession &);
    CassSession *session;
};

class CassandraBatch : public CInterface
{
public:
    CassandraBatch(CassBatch *_batch) : batch(_batch)
    {
    }
    ~CassandraBatch()
    {
        if (batch)
            cass_batch_free(batch);
    }
    inline operator CassBatch *() const
    {
        return batch;
    }
private:
    CassandraBatch(const CassandraBatch &);
    CassBatch *batch;
};

class CassandraStatement : public CInterface
{
public:
    CassandraStatement(CassStatement *_statement) : statement(_statement)
    {
    }
    CassandraStatement(const char *simple) : statement(cass_statement_new(cass_string_init(simple), 0))
    {
    }
    ~CassandraStatement()
    {
        if (statement)
            cass_statement_free(statement);
    }
    inline operator CassStatement *() const
    {
        return statement;
    }
private:
    CassandraStatement(const CassandraStatement &);
    CassStatement *statement;
};

class CassandraPrepared : public CInterfaceOf<IInterface>
{
public:
    CassandraPrepared(const CassPrepared *_prepared) : prepared(_prepared)
    {
    }
    ~CassandraPrepared()
    {
        if (prepared)
            cass_prepared_free(prepared);
    }
    inline operator const CassPrepared *() const
    {
        return prepared;
    }
private:
    CassandraPrepared(const CassandraPrepared &);
    const CassPrepared *prepared;
};

class CassandraResult : public CInterface
{
public:
    CassandraResult(const CassResult *_result) : result(_result)
    {
    }
    ~CassandraResult()
    {
        if (result)
            cass_result_free(result);
    }
    inline operator const CassResult *() const
    {
        return result;
    }
private:
    CassandraResult(const CassandraResult &);
    const CassResult *result;
};

class CassandraIterator : public CInterface
{
public:
    CassandraIterator(CassIterator *_iterator) : iterator(_iterator)
    {
    }
    ~CassandraIterator()
    {
        if (iterator)
            cass_iterator_free(iterator);
    }
    inline operator CassIterator *() const
    {
        return iterator;
    }
private:
    CassandraIterator(const CassandraIterator &);
    CassIterator *iterator;
};

class CassandraCollection : public CInterface
{
public:
    CassandraCollection(CassCollection *_collection) : collection(_collection)
    {
    }
    ~CassandraCollection()
    {
        if (collection)
            cass_collection_free(collection);
    }
    inline operator CassCollection *() const
    {
        return collection;
    }
private:
    CassandraCollection(const CassandraCollection &);
    CassCollection *collection;
};

void check(CassError rc)
{
    if (rc != CASS_OK)
    {
        fail(cass_error_desc(rc));
    }
}

class CassandraStatementInfo : public CInterface
{
public:
    IMPLEMENT_IINTERFACE;
    CassandraStatementInfo(CassandraSession *_session, CassandraPrepared *_prepared, unsigned _numBindings, CassBatchType _batchMode)
    : session(_session), prepared(_prepared), numBindings(_numBindings), batchMode(_batchMode)
    {
        assertex(prepared && *prepared);
        statement.setown(new CassandraStatement(cass_prepared_bind(*prepared)));
    }
    ~CassandraStatementInfo()
    {
        stop();
    }
    inline void stop()
    {
        iterator.clear();
        result.clear();
        prepared.clear();
    }
    bool next()
    {
        if (!iterator)
            return false;
        return cass_iterator_next(*iterator);
    }
    void startStream()
    {
        if (batchMode != (CassBatchType) -1)
        {
            batch.setown(new CassandraBatch(cass_batch_new(batchMode)));
            statement.setown(new CassandraStatement(cass_prepared_bind(*prepared)));
        }
    }
    void endStream()
    {
        if (batch)
        {
            CassandraFuture future(cass_session_execute_batch(*session, *batch));
            future.wait("execute");
            result.setown(new CassandraResult(cass_future_get_result(future)));
            assertex (rowCount() == 0);
        }
    }
    void execute()
    {
        assertex(statement && *statement);
        if (batch)
        {
            check(cass_batch_add_statement(*batch, *statement));
            statement.setown(new CassandraStatement(cass_prepared_bind(*prepared)));
        }
        else
        {
            CassandraFuture future(cass_session_execute(*session, *statement));
            future.wait("execute");
            result.setown(new CassandraResult(cass_future_get_result(future)));
            if (rowCount() > 0)
                iterator.setown(new CassandraIterator(cass_iterator_from_result(*result)));
        }
    }
    inline size_t rowCount() const
    {
        return cass_result_row_count(*result);
    }
    inline bool hasResult() const
    {
        return result != NULL;
    }
    inline const CassRow *queryRow() const
    {
        assertex(iterator && *iterator);
        return cass_iterator_get_row(*iterator);
    }
    inline CassStatement *queryStatement() const
    {
        assertex(statement && *statement);
        return *statement;
    }
protected:
    Linked<CassandraSession> session;
    Linked<CassandraPrepared> prepared;
    Owned<CassandraBatch> batch;
    Owned<CassandraStatement> statement;
    Owned<CassandraResult> result;
    Owned<CassandraIterator> iterator;
    unsigned numBindings;
    CassBatchType batchMode;
};

// Conversions from Cassandra values to ECL data

static const char *getTypeName(CassValueType type)
{
    switch (type)
    {
    case CASS_VALUE_TYPE_CUSTOM: return "CUSTOM";
    case CASS_VALUE_TYPE_ASCII: return "ASCII";
    case CASS_VALUE_TYPE_BIGINT: return "BIGINT";
    case CASS_VALUE_TYPE_BLOB: return "BLOB";
    case CASS_VALUE_TYPE_BOOLEAN: return "BOOLEAN";
    case CASS_VALUE_TYPE_COUNTER: return "COUNTER";
    case CASS_VALUE_TYPE_DECIMAL: return "DECIMAL";
    case CASS_VALUE_TYPE_DOUBLE: return "DOUBLE";
    case CASS_VALUE_TYPE_FLOAT: return "FLOAT";
    case CASS_VALUE_TYPE_INT: return "INT";
    case CASS_VALUE_TYPE_TEXT: return "TEXT";
    case CASS_VALUE_TYPE_TIMESTAMP: return "TIMESTAMP";
    case CASS_VALUE_TYPE_UUID: return "UUID";
    case CASS_VALUE_TYPE_VARCHAR: return "VARCHAR";
    case CASS_VALUE_TYPE_VARINT: return "VARINT";
    case CASS_VALUE_TYPE_TIMEUUID: return "TIMEUUID";
    case CASS_VALUE_TYPE_INET: return "INET";
    case CASS_VALUE_TYPE_LIST: return "LIST";
    case CASS_VALUE_TYPE_MAP: return "MAP";
    case CASS_VALUE_TYPE_SET: return "SET";
    default: return "UNKNOWN";
    }
}
static void typeError(const char *expected, const CassValue *value, const RtlFieldInfo *field) __attribute__((noreturn));

static void typeError(const char *expected, const CassValue *value, const RtlFieldInfo *field)
{
    VStringBuffer msg("cassandra: type mismatch - %s expected", expected);
    if (field)
        msg.appendf(" for field %s", field->name->str());
    if (value)
        msg.appendf(", received %s", getTypeName(cass_value_type(value)));
    rtlFail(0, msg.str());
}

static bool isInteger(const CassValueType t)
{
    switch (t)
    {
    case CASS_VALUE_TYPE_TIMESTAMP:
    case CASS_VALUE_TYPE_INT:
    case CASS_VALUE_TYPE_BIGINT:
    case CASS_VALUE_TYPE_COUNTER:
    case CASS_VALUE_TYPE_VARINT:
        return true;
    default:
        return false;
    }
}

static bool isString(CassValueType t)
{
    switch (t)
    {
    case CASS_VALUE_TYPE_VARCHAR:
    case CASS_VALUE_TYPE_TEXT:
    case CASS_VALUE_TYPE_ASCII:
        return true;
    default:
        return false;
    }
}

// when extracting elements of a set, field will point at the SET info- we want to get the typeInfo for the element type
static const RtlTypeInfo *getFieldBaseType(const RtlFieldInfo *field)
{
   const RtlTypeInfo *type = field->type;
   if ((type->fieldType & RFTMkind) == type_set)
       return type->queryChildType();
   else
       return type;
}

static int getNumFields(const RtlTypeInfo *record)
{
    int count = 0;
    const RtlFieldInfo * const *fields = record->queryFields();
    assertex(fields);
    while (*fields++)
        count++;
    return count;
}

static bool getBooleanResult(const RtlFieldInfo *field, const CassValue *value)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        return p.boolResult;
    }
    if (cass_value_type(value) != CASS_VALUE_TYPE_BOOLEAN)
        typeError("boolean", value, field);
    cass_bool_t output;
    check(cass_value_get_bool(value, &output));
    return output != cass_false;
}

static void getDataResult(const RtlFieldInfo *field, const CassValue *value, size32_t &chars, void * &result)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        rtlStrToDataX(chars, result, p.resultChars, p.stringResult);
        return;
    }
    // We COULD require that the field being retrieved is a blob - but Cassandra seems happy to use any field here, and
    // it seems like it could be more useful to support anything
    // if (cass_value_type(value) != CASS_VALUE_TYPE_BLOB)
    //     typeError("blob", value, field);
    CassBytes bytes;
    check(cass_value_get_bytes(value, &bytes));
    rtlStrToDataX(chars, result, bytes.size, bytes.data);
}

static __int64 getSignedResult(const RtlFieldInfo *field, const CassValue *value);
static unsigned __int64 getUnsignedResult(const RtlFieldInfo *field, const CassValue *value);

static double getRealResult(const RtlFieldInfo *field, const CassValue *value)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        return p.doubleResult;
    }
    else if (isInteger(cass_value_type(value)))
        return (double) getSignedResult(field, value);
    else switch (cass_value_type(value))
    {
    case CASS_VALUE_TYPE_FLOAT:
    {
        cass_float_t output_f;
        check(cass_value_get_float(value, &output_f));
        return output_f;
    }
    case CASS_VALUE_TYPE_DOUBLE:
    {
        cass_double_t output_d;
        check(cass_value_get_double(value, &output_d));
        return output_d;
    }
    default:
        typeError("double", value, field);
    }
}

static __int64 getSignedResult(const RtlFieldInfo *field, const CassValue *value)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        return p.intResult;
    }
    switch (cass_value_type(value))
    {
    case CASS_VALUE_TYPE_INT:
    {
        cass_int32_t output;
        check(cass_value_get_int32(value, &output));
        return output;
    }
    case CASS_VALUE_TYPE_TIMESTAMP:
    case CASS_VALUE_TYPE_BIGINT:
    case CASS_VALUE_TYPE_COUNTER:
    case CASS_VALUE_TYPE_VARINT:
    {
        cass_int64_t output;
        check(cass_value_get_int64(value, &output));
        return output;
    }
    default:
        typeError("integer", value, field);
    }
}

static unsigned __int64 getUnsignedResult(const RtlFieldInfo *field, const CassValue *value)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        return p.uintResult;
    }
    return (__uint64) getSignedResult(field, value);
}

static void getStringResult(const RtlFieldInfo *field, const CassValue *value, size32_t &chars, char * &result)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        rtlStrToStrX(chars, result, p.resultChars, p.stringResult);
        return;
    }
    switch (cass_value_type(value))
    {
    case CASS_VALUE_TYPE_ASCII:
    {
        CassString output;
        check(cass_value_get_string(value, &output));
        const char *text = output.data;
        unsigned long bytes = output.length;
        rtlStrToStrX(chars, result, bytes, text);
        break;
    }
    case CASS_VALUE_TYPE_VARCHAR:
    case CASS_VALUE_TYPE_TEXT:
    {
        CassString output;
        check(cass_value_get_string(value, &output));
        const char *text = output.data;
        unsigned long bytes = output.length;
        unsigned numchars = rtlUtf8Length(bytes, text);
        rtlUtf8ToStrX(chars, result, numchars, text);
        break;
    }
    default:
        typeError("string", value, field);
    }
}

static void getUTF8Result(const RtlFieldInfo *field, const CassValue *value, size32_t &chars, char * &result)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        rtlUtf8ToUtf8X(chars, result, p.resultChars, p.stringResult);
        return;
    }
    switch (cass_value_type(value))
    {
    case CASS_VALUE_TYPE_ASCII:
    {
        CassString output;
        check(cass_value_get_string(value, &output));
        const char *text = output.data;
        unsigned long bytes = output.length;
        rtlStrToUtf8X(chars, result, bytes, text);
        break;
    }
    case CASS_VALUE_TYPE_VARCHAR:
    case CASS_VALUE_TYPE_TEXT:
    {
        CassString output;
        check(cass_value_get_string(value, &output));
        const char *text = output.data;
        unsigned long bytes = output.length;
        unsigned numchars = rtlUtf8Length(bytes, text);
        rtlUtf8ToUtf8X(chars, result, numchars, text);
        break;
    }
    default:
        typeError("string", value, field);
    }
}

static void getUnicodeResult(const RtlFieldInfo *field, const CassValue *value, size32_t &chars, UChar * &result)
{
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        rtlUnicodeToUnicodeX(chars, result, p.resultChars, p.unicodeResult);
        return;
    }
    switch (cass_value_type(value))
    {
    case CASS_VALUE_TYPE_ASCII:
    {
        CassString output;
        check(cass_value_get_string(value, &output));
        const char *text = output.data;
        unsigned long bytes = output.length;
        rtlStrToUnicodeX(chars, result, bytes, text);
        break;
    }
    case CASS_VALUE_TYPE_VARCHAR:
    case CASS_VALUE_TYPE_TEXT:
    {
        CassString output;
        check(cass_value_get_string(value, &output));
        const char *text = output.data;
        unsigned long bytes = output.length;
        unsigned numchars = rtlUtf8Length(bytes, text);
        rtlUtf8ToUnicodeX(chars, result, numchars, text);
        break;
    }
    default:
        typeError("string", value, field);
    }
}

static void getDecimalResult(const RtlFieldInfo *field, const CassValue *value, Decimal &result)
{
    // Note - Cassandra has a decimal type, but it's not particularly similar to the ecl one. Map to string for now, as we do in MySQL
    if (cass_value_is_null(value))
    {
        NullFieldProcessor p(field);
        result.set(p.decimalResult);
        return;
    }
    size32_t chars;
    rtlDataAttr tempStr;
    cassandraembed::getStringResult(field, value, chars, tempStr.refstr());
    result.setString(chars, tempStr.getstr());
    if (field)
    {
        RtlDecimalTypeInfo *dtype = (RtlDecimalTypeInfo *) field->type;
        result.setPrecision(dtype->getDecimalDigits(), dtype->getDecimalPrecision());
    }
}

// A CassandraRowBuilder object is used to construct an ECL row from a Cassandra row

class CassandraRowBuilder : public CInterfaceOf<IFieldSource>
{
public:
    CassandraRowBuilder(const CassandraStatementInfo *_stmtInfo)
    : stmtInfo(_stmtInfo), colIdx(0), numIteratorFields(0), nextIteratedField(0)
    {
    }
    virtual bool getBooleanResult(const RtlFieldInfo *field)
    {
        return cassandraembed::getBooleanResult(field, nextField(field));
    }
    virtual void getDataResult(const RtlFieldInfo *field, size32_t &len, void * &result)
    {
        cassandraembed::getDataResult(field, nextField(field), len, result);
    }
    virtual double getRealResult(const RtlFieldInfo *field)
    {
        return cassandraembed::getRealResult(field, nextField(field));
    }
    virtual __int64 getSignedResult(const RtlFieldInfo *field)
    {
        return cassandraembed::getSignedResult(field, nextField(field));
    }
    virtual unsigned __int64 getUnsignedResult(const RtlFieldInfo *field)
    {
        return cassandraembed::getUnsignedResult(field, nextField(field));
    }
    virtual void getStringResult(const RtlFieldInfo *field, size32_t &chars, char * &result)
    {
        cassandraembed::getStringResult(field, nextField(field), chars, result);
    }
    virtual void getUTF8Result(const RtlFieldInfo *field, size32_t &chars, char * &result)
    {
        cassandraembed::getUTF8Result(field, nextField(field), chars, result);
    }
    virtual void getUnicodeResult(const RtlFieldInfo *field, size32_t &chars, UChar * &result)
    {
        cassandraembed::getUnicodeResult(field, nextField(field), chars, result);
    }
    virtual void getDecimalResult(const RtlFieldInfo *field, Decimal &value)
    {
        cassandraembed::getDecimalResult(field, nextField(field), value);
    }

    virtual void processBeginSet(const RtlFieldInfo * field, bool &isAll)
    {
        isAll = false;
        iterator.setown(new CassandraIterator(cass_iterator_from_collection(nextField(field))));
    }
    virtual bool processNextSet(const RtlFieldInfo * field)
    {
        numIteratorFields = 1;
        return *iterator && cass_iterator_next(*iterator); // If field was NULL, we'll have a NULL iterator (representing an empty set/list)
        // Can't distinguish empty set from NULL field, so assume the former (rather than trying to deliver the default value for the set field)
    }
    virtual void processBeginDataset(const RtlFieldInfo * field)
    {
        numIteratorFields = getNumFields(field->type->queryChildType());
        switch (numIteratorFields)
        {
        case 1:
            iterator.setown(new CassandraIterator(cass_iterator_from_collection(nextField(field))));
            break;
        case 2:
            iterator.setown(new CassandraIterator(cass_iterator_from_map(nextField(field))));
            break;
        default:
            UNSUPPORTED("Nested datasets with > 2 fields");
        }
    }
    virtual void processBeginRow(const RtlFieldInfo * field)
    {
    }
    virtual bool processNextRow(const RtlFieldInfo * field)
    {
        nextIteratedField = 0;
        return *iterator && cass_iterator_next(*iterator); // If field was NULL, we'll have a NULL iterator (representing an empty set/list/map)
        // Can't distinguish empty set from NULL field, so assume the former (rather than trying to deliver the default value for the set field)
    }
    virtual void processEndSet(const RtlFieldInfo * field)
    {
        iterator.clear();
        numIteratorFields = 0;
    }
    virtual void processEndDataset(const RtlFieldInfo * field)
    {
        iterator.clear();
        numIteratorFields = 0;
    }
    virtual void processEndRow(const RtlFieldInfo * field)
    {
    }
protected:
    const CassValue *nextField(const RtlFieldInfo * field)
    {
        const CassValue *ret;
        if (iterator)
        {
            switch (numIteratorFields)
            {
            case 1:
                ret = cass_iterator_get_value(*iterator);
                break;
            case 2:
                if (nextIteratedField==0)
                    ret = cass_iterator_get_map_key(*iterator);
                else
                    ret = cass_iterator_get_map_value(*iterator);
                nextIteratedField++;
                break;
            default:
                throwUnexpected();
            }
        }
        else
            ret = cass_row_get_column(stmtInfo->queryRow(), colIdx++);
        if (!ret)
            failx("Too many fields in ECL output row, reading field %s", field->name->getAtomNamePtr());
        return ret;
    }
    const CassandraStatementInfo *stmtInfo;
    Owned<CassandraIterator> iterator;
    int colIdx;
    int numIteratorFields;
    int nextIteratedField;
};

// Bind Cassandra columns from an ECL record

class CassandraRecordBinder : public CInterfaceOf<IFieldProcessor>
{
public:
    CassandraRecordBinder(const IContextLogger &_logctx, const RtlTypeInfo *_typeInfo, const CassandraStatementInfo *_stmtInfo, int _firstParam)
      : logctx(_logctx), typeInfo(_typeInfo), stmtInfo(_stmtInfo), firstParam(_firstParam), dummyField("<row>", NULL, typeInfo), thisParam(_firstParam)
    {
    }
    int numFields()
    {
        int count = 0;
        const RtlFieldInfo * const *fields = typeInfo->queryFields();
        assertex(fields);
        while (*fields++)
            count++;
        return count;
    }
    void processRow(const byte *row)
    {
        thisParam = firstParam;
        typeInfo->process(row, row, &dummyField, *this);   // Bind the variables for the current row
    }
    virtual void processString(unsigned len, const char *value, const RtlFieldInfo * field)
    {
        size32_t utf8chars;
        rtlDataAttr utfText;
        rtlStrToUtf8X(utf8chars, utfText.refstr(), len, value);
        if (collection)
            checkBind(cass_collection_append_string(*collection,
                                                    cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr()))),
                      field);
        else
            checkBind(cass_statement_bind_string(stmtInfo->queryStatement(),
                                                 checkNextParam(field),
                                                 cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr()))),
                      field);
    }
    virtual void processBool(bool value, const RtlFieldInfo * field)
    {
        if (collection)
            checkBind(cass_collection_append_bool(*collection, value ? cass_true : cass_false), field);
        else
            checkBind(cass_statement_bind_bool(stmtInfo->queryStatement(), checkNextParam(field), value ? cass_true : cass_false), field);
    }
    virtual void processData(unsigned len, const void *value, const RtlFieldInfo * field)
    {
        if (collection)
            checkBind(cass_collection_append_bytes(*collection, cass_bytes_init((const cass_byte_t*) value, len)), field);
        else
            checkBind(cass_statement_bind_bytes(stmtInfo->queryStatement(), checkNextParam(field), cass_bytes_init((const cass_byte_t*) value, len)), field);
    }
    virtual void processInt(__int64 value, const RtlFieldInfo * field)
    {
        if (getFieldBaseType(field)->size(NULL,NULL)>4)
        {
            if (collection)
                checkBind(cass_collection_append_int64(*collection, value), field);
            else
                checkBind(cass_statement_bind_int64(stmtInfo->queryStatement(), checkNextParam(field), value), field);
        }
        else
        {
            if (collection)
                checkBind(cass_collection_append_int32(*collection, value), field);
            else
                checkBind(cass_statement_bind_int32(stmtInfo->queryStatement(), checkNextParam(field), value), field);
        }
    }
    virtual void processUInt(unsigned __int64 value, const RtlFieldInfo * field)
    {
        UNSUPPORTED("UNSIGNED columns");
    }
    virtual void processReal(double value, const RtlFieldInfo * field)
    {
        if (getFieldBaseType(field)->size(NULL,NULL)>4)
        {
            if (collection)
                checkBind(cass_collection_append_double(*collection, value), field);
            else
                checkBind(cass_statement_bind_double(stmtInfo->queryStatement(), checkNextParam(field), value), field);
        }
        else
        {
            if (collection)
                checkBind(cass_collection_append_float(*collection, (float) value), field);
            else
                checkBind(cass_statement_bind_float(stmtInfo->queryStatement(), checkNextParam(field), (float) value), field);
        }
    }
    virtual void processDecimal(const void *value, unsigned digits, unsigned precision, const RtlFieldInfo * field)
    {
        Decimal val;
        size32_t bytes;
        rtlDataAttr decText;
        val.setDecimal(digits, precision, value);
        val.getStringX(bytes, decText.refstr());
        processUtf8(bytes, decText.getstr(), field);
    }
    virtual void processUDecimal(const void *value, unsigned digits, unsigned precision, const RtlFieldInfo * field)
    {
        UNSUPPORTED("UNSIGNED decimals");
    }
    virtual void processUnicode(unsigned chars, const UChar *value, const RtlFieldInfo * field)
    {
        size32_t utf8chars;
        rtlDataAttr utfText;
        rtlUnicodeToUtf8X(utf8chars, utfText.refstr(), chars, value);
        if (collection)
            checkBind(cass_collection_append_string(*collection,
                                                    cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr()))),
                      field);
        else
            checkBind(cass_statement_bind_string(stmtInfo->queryStatement(),
                                                 checkNextParam(field),
                                                 cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr()))),
                      field);
    }
    virtual void processQString(unsigned len, const char *value, const RtlFieldInfo * field)
    {
        size32_t charCount;
        rtlDataAttr text;
        rtlQStrToStrX(charCount, text.refstr(), len, value);
        processUtf8(charCount, text.getstr(), field);
    }
    virtual void processUtf8(unsigned chars, const char *value, const RtlFieldInfo * field)
    {
        if (collection)
            checkBind(cass_collection_append_string(*collection, cass_string_init2(value, rtlUtf8Size(chars, value))), field);
        else
            checkBind(cass_statement_bind_string(stmtInfo->queryStatement(), checkNextParam(field), cass_string_init2(value, rtlUtf8Size(chars, value))), field);
    }

    virtual bool processBeginSet(const RtlFieldInfo * field, unsigned numElements, bool isAll, const byte *data)
    {
        if (isAll)
            UNSUPPORTED("SET(ALL)");
        collection.setown(new CassandraCollection(cass_collection_new(CASS_COLLECTION_TYPE_SET, numElements)));
        return true;
    }
    virtual bool processBeginDataset(const RtlFieldInfo * field, unsigned numRows)
    {
        // If there's a single field, assume we are mapping to a SET/LIST
        // If there are two, assume it's a MAP
        // Otherwise, fail
        int numFields = getNumFields(field->type->queryChildType());
        if (numFields < 1 || numFields > 2)
        {
            UNSUPPORTED("Nested datasets with > 2 fields");
        }
        collection.setown(new CassandraCollection(cass_collection_new(numFields==1 ? CASS_COLLECTION_TYPE_SET : CASS_COLLECTION_TYPE_MAP, numRows)));
        return true;
    }
    virtual bool processBeginRow(const RtlFieldInfo * field)
    {
        return true;
    }
    virtual void processEndSet(const RtlFieldInfo * field)
    {
        checkBind(cass_statement_bind_collection(stmtInfo->queryStatement(), checkNextParam(field), *collection), field);
        collection.clear();
    }
    virtual void processEndDataset(const RtlFieldInfo * field)
    {
        checkBind(cass_statement_bind_collection(stmtInfo->queryStatement(), checkNextParam(field), *collection), field);
        collection.clear();
    }
    virtual void processEndRow(const RtlFieldInfo * field)
    {
    }
protected:
    inline unsigned checkNextParam(const RtlFieldInfo * field)
    {
        if (logctx.queryTraceLevel() > 4)
            logctx.CTXLOG("Binding %s to %d", field->name->str(), thisParam);
        return thisParam++;
    }
    inline void checkBind(CassError rc, const RtlFieldInfo * field)
    {
        if (rc != CASS_OK)
        {
            failx("While binding parameter %s: %s", field->name->getAtomNamePtr(), cass_error_desc(rc));
        }
    }
    const RtlTypeInfo *typeInfo;
    const CassandraStatementInfo *stmtInfo;
    Owned<CassandraCollection> collection;
    const IContextLogger &logctx;
    int firstParam;
    RtlFieldStrInfo dummyField;
    int thisParam;
};

//
class CassandraDatasetBinder : public CassandraRecordBinder
{
public:
    CassandraDatasetBinder(const IContextLogger &_logctx, IRowStream * _input, const RtlTypeInfo *_typeInfo, const CassandraStatementInfo *_stmt, int _firstParam)
      : input(_input), CassandraRecordBinder(_logctx, _typeInfo, _stmt, _firstParam)
    {
    }
    bool bindNext()
    {
        roxiemem::OwnedConstRoxieRow nextRow = (const byte *) input->ungroupedNextRow();
        if (!nextRow)
            return false;
        processRow((const byte *) nextRow.get());   // Bind the variables for the current row
        return true;
    }
    void executeAll(CassandraStatementInfo *stmtInfo)
    {
        stmtInfo->startStream();
        while (bindNext())
        {
            stmtInfo->execute();
        }
        stmtInfo->endStream();
    }
protected:
    Owned<IRowStream> input;
};

// A Cassandra function that returns a dataset will return a CassandraRowStream object that can be
// interrogated to return each row of the result in turn

class CassandraRowStream : public CInterfaceOf<IRowStream>
{
public:
    CassandraRowStream(CassandraDatasetBinder *_inputStream, CassandraStatementInfo *_stmtInfo, IEngineRowAllocator *_resultAllocator)
    : inputStream(_inputStream), stmtInfo(_stmtInfo), resultAllocator(_resultAllocator)
    {
        executePending = true;
        eof = false;
    }
    virtual const void *nextRow()
    {
        // A little complex when streaming data in as well as out - want to execute for every input record
        if (eof)
            return NULL;
        loop
        {
            if (executePending)
            {
                executePending = false;
                if (inputStream && !inputStream->bindNext())
                {
                    noteEOF();
                    return NULL;
                }
                stmtInfo->execute();
            }
            if (stmtInfo->next())
                break;
            if (inputStream)
                executePending = true;
            else
            {
                noteEOF();
                return NULL;
            }
        }
        RtlDynamicRowBuilder rowBuilder(resultAllocator);
        CassandraRowBuilder cassandraRowBuilder(stmtInfo);
        const RtlTypeInfo *typeInfo = resultAllocator->queryOutputMeta()->queryTypeInfo();
        assertex(typeInfo);
        RtlFieldStrInfo dummyField("<row>", NULL, typeInfo);
        size32_t len = typeInfo->build(rowBuilder, 0, &dummyField, cassandraRowBuilder);
        return rowBuilder.finalizeRowClear(len);
    }
    virtual void stop()
    {
        resultAllocator.clear();
        stmtInfo->stop();
    }

protected:
    void noteEOF()
    {
        if (!eof)
        {
            eof = true;
            stop();
        }
    }
    Linked<CassandraDatasetBinder> inputStream;
    Linked<CassandraStatementInfo> stmtInfo;
    Linked<IEngineRowAllocator> resultAllocator;
    bool executePending;
    bool eof;
};

// Each call to a Cassandra function will use a new CassandraEmbedFunctionContext object

class CassandraEmbedFunctionContext : public CInterfaceOf<IEmbedFunctionContext>
{
public:
    CassandraEmbedFunctionContext(const IContextLogger &_logctx, unsigned _flags, const char *options)
      : logctx(_logctx), flags(_flags), nextParam(0), numParams(0)
    {
        StringArray opts;
        opts.appendList(options, ",");
        cluster.setown(new CassandraCluster(cass_cluster_new()));
        cluster->setOptions(opts);
        session.setown(new CassandraSession(cass_session_new()));
        CassandraFuture future(cluster->keyspace.isEmpty() ? cass_session_connect(*session, *cluster) : cass_session_connect_keyspace(*session, *cluster, cluster->keyspace));
        future.wait("connect");
    }
    virtual bool getBooleanResult()
    {
        bool ret = cassandraembed::getBooleanResult(NULL, getScalarResult());
        checkSingleRow();
        return ret;
    }
    virtual void getDataResult(size32_t &len, void * &result)
    {
        cassandraembed::getDataResult(NULL, getScalarResult(), len, result);
        checkSingleRow();
    }
    virtual double getRealResult()
    {
        double ret = cassandraembed::getRealResult(NULL, getScalarResult());
        checkSingleRow();
        return ret;
    }
    virtual __int64 getSignedResult()
    {
        __int64 ret = cassandraembed::getSignedResult(NULL, getScalarResult());
        checkSingleRow();
        return ret;
    }
    virtual unsigned __int64 getUnsignedResult()
    {
        unsigned __int64 ret = cassandraembed::getUnsignedResult(NULL, getScalarResult());
        checkSingleRow();
        return ret;
    }
    virtual void getStringResult(size32_t &chars, char * &result)
    {
        cassandraembed::getStringResult(NULL, getScalarResult(), chars, result);
        checkSingleRow();
    }
    virtual void getUTF8Result(size32_t &chars, char * &result)
    {
        cassandraembed::getUTF8Result(NULL, getScalarResult(), chars, result);
        checkSingleRow();
    }
    virtual void getUnicodeResult(size32_t &chars, UChar * &result)
    {
        cassandraembed::getUnicodeResult(NULL, getScalarResult(), chars, result);
        checkSingleRow();
    }
    virtual void getDecimalResult(Decimal &value)
    {
        cassandraembed::getDecimalResult(NULL, getScalarResult(), value);
        checkSingleRow();
    }
    virtual void getSetResult(bool & __isAllResult, size32_t & __resultBytes, void * & __result, int elemType, size32_t elemSize)
    {
        CassandraIterator iterator(cass_iterator_from_collection(getScalarResult()));
        rtlRowBuilder out;
        byte *outData = NULL;
        size32_t outBytes = 0;
        while (cass_iterator_next(iterator))
        {
            const CassValue *value = cass_iterator_get_value(iterator);
            assertex(value);
            if (elemSize != UNKNOWN_LENGTH)
            {
                out.ensureAvailable(outBytes + elemSize);
                outData = out.getbytes() + outBytes;
            }
            switch ((type_t) elemType)
            {
            case type_int:
                rtlWriteInt(outData, cassandraembed::getSignedResult(NULL, value), elemSize);
                break;
            case type_unsigned:
                rtlWriteInt(outData, cassandraembed::getUnsignedResult(NULL, value), elemSize);
                break;
            case type_real:
                if (elemSize == sizeof(double))
                    * (double *) outData = cassandraembed::getRealResult(NULL, value);
                else
                {
                    assertex(elemSize == sizeof(float));
                    * (float *) outData = (float) cassandraembed::getRealResult(NULL, value);
                }
                break;
            case type_boolean:
                assertex(elemSize == sizeof(bool));
                * (bool *) outData = cassandraembed::getBooleanResult(NULL, value);
                break;
            case type_string:
            case type_varstring:
            {
                rtlDataAttr str;
                size32_t lenBytes;
                cassandraembed::getStringResult(NULL, value, lenBytes, str.refstr());
                if (elemSize == UNKNOWN_LENGTH)
                {
                    if (elemType == type_string)
                    {
                        out.ensureAvailable(outBytes + lenBytes + sizeof(size32_t));
                        outData = out.getbytes() + outBytes;
                        * (size32_t *) outData = lenBytes;
                        rtlStrToStr(lenBytes, outData+sizeof(size32_t), lenBytes, str.getstr());
                        outBytes += lenBytes + sizeof(size32_t);
                    }
                    else
                    {
                        out.ensureAvailable(outBytes + lenBytes + 1);
                        outData = out.getbytes() + outBytes;
                        rtlStrToVStr(0, outData, lenBytes, str.getstr());
                        outBytes += lenBytes + 1;
                    }
                }
                else
                {
                    if (elemType == type_string)
                        rtlStrToStr(elemSize, outData, lenBytes, str.getstr());
                    else
                        rtlStrToVStr(elemSize, outData, lenBytes, str.getstr());  // Fixed size null terminated strings... weird.
                }
                break;
            }
            case type_unicode:
            case type_utf8:
            {
                rtlDataAttr str;
                size32_t lenChars;
                cassandraembed::getUTF8Result(NULL, value, lenChars, str.refstr());
                const char * text =  str.getstr();
                size32_t lenBytes = rtlUtf8Size(lenChars, text);
                if (elemType == type_utf8)
                {
                    assertex (elemSize == UNKNOWN_LENGTH);
                    out.ensureAvailable(outBytes + lenBytes + sizeof(size32_t));
                    outData = out.getbytes() + outBytes;
                    * (size32_t *) outData = lenChars;
                    rtlStrToStr(lenBytes, outData+sizeof(size32_t), lenBytes, text);
                    outBytes += lenBytes + sizeof(size32_t);
                }
                else
                {
                    if (elemSize == UNKNOWN_LENGTH)
                    {
                        // You can't assume that number of chars in utf8 matches number in unicode16 ...
                        size32_t numchars16;
                        rtlDataAttr unicode16;
                        rtlUtf8ToUnicodeX(numchars16, unicode16.refustr(), lenChars, text);
                        out.ensureAvailable(outBytes + numchars16*sizeof(UChar) + sizeof(size32_t));
                        outData = out.getbytes() + outBytes;
                        * (size32_t *) outData = numchars16;
                        rtlUnicodeToUnicode(numchars16, (UChar *) (outData+sizeof(size32_t)), numchars16, unicode16.getustr());
                        outBytes += numchars16*sizeof(UChar) + sizeof(size32_t);
                    }
                    else
                        rtlUtf8ToUnicode(elemSize / sizeof(UChar), (UChar *) outData, lenChars, text);
                }
                break;
            }
            default:
                fail("type mismatch - unsupported return type");
            }
            if (elemSize != UNKNOWN_LENGTH)
                outBytes += elemSize;
        }
        __isAllResult = false;
        __resultBytes = outBytes;
        __result = out.detachdata();
    }
    virtual IRowStream *getDatasetResult(IEngineRowAllocator * _resultAllocator)
    {
        return new CassandraRowStream(inputStream, stmtInfo, _resultAllocator);
    }
    virtual byte * getRowResult(IEngineRowAllocator * _resultAllocator)
    {
        if (!stmtInfo->hasResult() || stmtInfo->rowCount() != 1)
            typeError("row", NULL, NULL);
        CassandraRowStream stream(NULL, stmtInfo, _resultAllocator);
        roxiemem::OwnedConstRoxieRow ret = stream.nextRow();
        stream.stop();
        if (ret ==  NULL)  // Check for exactly one returned row
            typeError("row", NULL, NULL);
        return (byte *) ret.getClear();
    }
    virtual size32_t getTransformResult(ARowBuilder & rowBuilder)
    {
        if (!stmtInfo->hasResult() || stmtInfo->rowCount() != 1)
            typeError("row", NULL, NULL);
        if (!stmtInfo->next())
            fail("Failed to read row");
        CassandraRowBuilder cassandraRowBuilder(stmtInfo);
        const RtlTypeInfo *typeInfo = rowBuilder.queryAllocator()->queryOutputMeta()->queryTypeInfo();
        assertex(typeInfo);
        RtlFieldStrInfo dummyField("<row>", NULL, typeInfo);
        return typeInfo->build(rowBuilder, 0, &dummyField, cassandraRowBuilder);
    }
    virtual void bindRowParam(const char *name, IOutputMetaData & metaVal, byte *val)
    {
        CassandraRecordBinder binder(logctx, metaVal.queryTypeInfo(), stmtInfo, nextParam);
        binder.processRow(val);
        nextParam += binder.numFields();
    }
    virtual void bindDatasetParam(const char *name, IOutputMetaData & metaVal, IRowStream * val)
    {
        // We only support a single dataset parameter...
        // MORE - look into batch?
        if (inputStream)
        {
            fail("At most one dataset parameter supported");
        }
        inputStream.setown(new CassandraDatasetBinder(logctx, LINK(val), metaVal.queryTypeInfo(), stmtInfo, nextParam));
        nextParam += inputStream->numFields();
    }

    virtual void bindBooleanParam(const char *name, bool val)
    {
        checkBind(cass_statement_bind_bool(stmtInfo->queryStatement(), checkNextParam(name), val ? cass_true : cass_false), name);
    }
    virtual void bindDataParam(const char *name, size32_t len, const void *val)
    {
        checkBind(cass_statement_bind_bytes(stmtInfo->queryStatement(), checkNextParam(name), cass_bytes_init((const cass_byte_t*) val, len)), name);
    }
    virtual void bindFloatParam(const char *name, float val)
    {
        checkBind(cass_statement_bind_float(stmtInfo->queryStatement(), checkNextParam(name), val), name);
    }
    virtual void bindRealParam(const char *name, double val)
    {
        checkBind(cass_statement_bind_double(stmtInfo->queryStatement(), checkNextParam(name), val), name);
    }
    virtual void bindSignedSizeParam(const char *name, int size, __int64 val)
    {
        if (size > 4)
            checkBind(cass_statement_bind_int64(stmtInfo->queryStatement(), checkNextParam(name), val), name);
        else
            checkBind(cass_statement_bind_int32(stmtInfo->queryStatement(), checkNextParam(name), val), name);
    }
    virtual void bindSignedParam(const char *name, __int64 val)
    {
        bindSignedSizeParam(name, 8, val);
    }
    virtual void bindUnsignedSizeParam(const char *name, int size, unsigned __int64 val)
    {
        UNSUPPORTED("UNSIGNED columns");
    }
    virtual void bindUnsignedParam(const char *name, unsigned __int64 val)
    {
        UNSUPPORTED("UNSIGNED columns");
    }
    virtual void bindStringParam(const char *name, size32_t len, const char *val)
    {
        size32_t utf8chars;
        rtlDataAttr utfText;
        rtlStrToUtf8X(utf8chars, utfText.refstr(), len, val);
        checkBind(cass_statement_bind_string(stmtInfo->queryStatement(),
                                             checkNextParam(name),
                                             cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr()))),
                  name);
    }
    virtual void bindVStringParam(const char *name, const char *val)
    {
        bindStringParam(name, strlen(val), val);
    }
    virtual void bindUTF8Param(const char *name, size32_t chars, const char *val)
    {
        checkBind(cass_statement_bind_string(stmtInfo->queryStatement(), checkNextParam(name), cass_string_init2(val, rtlUtf8Size(chars, val))), name);
    }
    virtual void bindUnicodeParam(const char *name, size32_t chars, const UChar *val)
    {
        size32_t utf8chars;
        rtlDataAttr utfText;
        rtlUnicodeToUtf8X(utf8chars, utfText.refstr(), chars, val);
        checkBind(cass_statement_bind_string(stmtInfo->queryStatement(),
                                                 checkNextParam(name),
                                                 cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr()))),
                  name);
    }
    virtual void bindSetParam(const char *name, int elemType, size32_t elemSize, bool isAll, size32_t totalBytes, void *setData)
    {
        if (isAll)
            UNSUPPORTED("SET(ALL)");
        type_t typecode = (type_t) elemType;
        const byte *inData = (const byte *) setData;
        const byte *endData = inData + totalBytes;
        int numElems;
        if (elemSize == UNKNOWN_LENGTH)
        {
            numElems = 0;
            // Will need 2 passes to work out how many elements there are in the set :(
            while (inData < endData)
            {
                int thisSize;
                switch (elemType)
                {
                case type_varstring:
                    thisSize = strlen((const char *) inData) + 1;
                    break;
                case type_string:
                    thisSize = * (size32_t *) inData + sizeof(size32_t);
                    break;
                case type_unicode:
                    thisSize = (* (size32_t *) inData) * sizeof(UChar) + sizeof(size32_t);
                    break;
                case type_utf8:
                    thisSize = rtlUtf8Size(* (size32_t *) inData, inData + sizeof(size32_t)) + sizeof(size32_t);
                    break;
                default:
                    fail("Unsupported parameter type");
                    break;
                }
                inData += thisSize;
                numElems++;
            }
            inData = (const byte *) setData;
        }
        else
            numElems = totalBytes / elemSize;
        CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_SET, numElems));
        while (inData < endData)
        {
            size32_t thisSize = elemSize;
            CassError rc;
            switch (typecode)
            {
            case type_int:
                if (elemSize > 4)
                    rc = cass_collection_append_int64(collection, rtlReadInt(inData, elemSize));
                else
                    rc = cass_collection_append_int32(collection, rtlReadInt(inData, elemSize));
                break;
            case type_unsigned:
                UNSUPPORTED("UNSIGNED columns");
                break;
            case type_varstring:
            {
                size32_t numChars = strlen((const char *) inData);
                if (elemSize == UNKNOWN_LENGTH)
                    thisSize = numChars + 1;
                size32_t utf8chars;
                rtlDataAttr utfText;
                rtlStrToUtf8X(utf8chars, utfText.refstr(), numChars, (const char *) inData);
                rc = cass_collection_append_string(collection, cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr())));
                break;
            }
            case type_string:
            {
                if (elemSize == UNKNOWN_LENGTH)
                {
                    thisSize = * (size32_t *) inData;
                    inData += sizeof(size32_t);
                }
                size32_t utf8chars;
                rtlDataAttr utfText;
                rtlStrToUtf8X(utf8chars, utfText.refstr(), thisSize, (const char *) inData);
                rc = cass_collection_append_string(collection, cass_string_init2(utfText.getstr(), rtlUtf8Size(utf8chars, utfText.getstr())));
                break;
            }
            case type_real:
                if (elemSize == sizeof(double))
                    rc = cass_collection_append_double(collection, * (double *) inData);
                else
                    rc = cass_collection_append_float(collection, * (float *) inData);
                break;
            case type_boolean:
                assertex(elemSize == sizeof(bool));
                rc = cass_collection_append_bool(collection, *(bool*)inData ? cass_true : cass_false);
                break;
            case type_unicode:
            {
                if (elemSize == UNKNOWN_LENGTH)
                {
                    thisSize = (* (size32_t *) inData) * sizeof(UChar); // NOTE - it's in chars...
                    inData += sizeof(size32_t);
                }
                unsigned unicodeChars;
                rtlDataAttr unicode;
                rtlUnicodeToUtf8X(unicodeChars, unicode.refstr(), thisSize / sizeof(UChar), (const UChar *) inData);
                size32_t sizeBytes = rtlUtf8Size(unicodeChars, unicode.getstr());
                rc = cass_collection_append_string(collection, cass_string_init2(unicode.getstr(), sizeBytes));
                break;
            }
            case type_utf8:
            {
                assertex (elemSize == UNKNOWN_LENGTH);
                size32_t numChars = * (size32_t *) inData;
                inData += sizeof(size32_t);
                thisSize = rtlUtf8Size(numChars, inData);
                rc = cass_collection_append_string(collection, cass_string_init2((const char *) inData, thisSize));
                break;
            }
            case type_data:
                if (elemSize == UNKNOWN_LENGTH)
                {
                    thisSize = * (size32_t *) inData;
                    inData += sizeof(size32_t);
                }
                rc = cass_collection_append_bytes(collection, cass_bytes_init((const cass_byte_t*) inData, thisSize));
                break;
            }
            checkBind(rc, name);
            inData += thisSize;
        }
        checkBind(cass_statement_bind_collection(stmtInfo->queryStatement(),
                                                 checkNextParam(name),
                                                 collection),
                  name);
    }

    virtual void importFunction(size32_t lenChars, const char *text)
    {
        throwUnexpected();
    }
    virtual void compileEmbeddedScript(size32_t chars, const char *_script)
    {
        // Incoming script is not necessarily null terminated. Note that the chars refers to utf8 characters and not bytes.
        size32_t len = rtlUtf8Size(chars, _script);
        queryString.set(_script, len);
        const char *script = queryString.get(); // Now null terminated
        if ((flags & (EFnoreturn|EFnoparams)) == (EFnoreturn|EFnoparams))
        {
            loop
            {
                const char *nextScript = findUnquoted(script, ';');
                if (!nextScript)
                {
                    // script should be pointing at only trailing whitespace, else it's a "missing ;" error
                    break;
                }
                CassandraStatement statement(cass_statement_new(cass_string_init2(script, nextScript-script), 0));
                CassandraFuture future(cass_session_execute(*session, statement));
                future.wait("execute statement");
                script = nextScript;
            }
        }
        else
        {
            // MORE - can cache this, perhaps, if script is same as last time?
            CassandraFuture future(cass_session_prepare(*session, cass_string_init(script)));
            future.wait("prepare statement");
            Owned<CassandraPrepared> prepared = new CassandraPrepared(cass_future_get_prepared(future));
            if ((flags & EFnoparams) == 0)
                numParams = countBindings(script);
            else
                numParams = 0;
            stmtInfo.setown(new CassandraStatementInfo(session, prepared, numParams, cluster->batchMode));
        }
    }
    virtual void callFunction()
    {
        // Does not seem to be a way to check number of parameters expected...
        // if (nextParam != cass_statement_bind_count(stmtInfo))
        //    fail("Not enough parameters");
        try
        {
            if (stmtInfo && !stmtInfo->hasResult())
                lazyExecute();
        }
        catch (IException *E)
        {
            StringBuffer msg;
            E->errorMessage(msg);
            msg.appendf(" (processing query %s)", queryString.get());
            throw makeStringException(E->errorCode(), msg);
        }
    }
protected:
    void lazyExecute()
    {
        if (inputStream)
            inputStream->executeAll(stmtInfo);
        else
            stmtInfo->execute();
    }
    const CassValue *getScalarResult()
    {
        if (!stmtInfo->next())
            typeError("scalar", NULL, NULL);
        if (cass_row_get_column(stmtInfo->queryRow(), 1))
            typeError("scalar", NULL, NULL);
        const CassValue *result = cass_row_get_column(stmtInfo->queryRow(), 0);
        if (!result)
            typeError("scalar", NULL, NULL);
        return result;
    }
    void checkSingleRow()
    {
        if (stmtInfo->rowCount() != 1)
            typeError("scalar", NULL, NULL);
    }
    unsigned countBindings(const char *query)
    {
        unsigned queryCount = 0;
        while ((query = findUnquoted(query, '?')) != NULL)
            queryCount++;
        return queryCount;
    }
    const char *findUnquoted(const char *query, char searchFor)
    {
        // Note - returns pointer to char AFTER the first occurrence of searchFor outside of quotes

        char inStr = '\0';
        char ch;
        while ((ch = *query++) != 0)
        {
            if (ch == inStr)
                inStr = false;
            else switch (ch)
            {
            case '\'':
            case '"':
                inStr = ch;
                break;
            case '\\':
                if (inStr && *query)
                    query++;
                break;
            case '/':
                if (!inStr)
                {
                    if (*query=='/')
                    {
                        while (*query && *query != '\n')
                            query++;
                    }
                    else if (*query=='*')
                    {
                        query++;
                        loop
                        {
                            if (!*query)
                                fail("Unterminated comment in query string");
                            if (*query=='*' && query[1]=='/')
                            {
                                query+= 2;
                                break;
                            }
                            query++;
                        }
                    }
                }
                break;
            default:
                if (!inStr && ch==searchFor)
                    return query;
                break;
            }
        }
        return NULL;
    }
    inline unsigned checkNextParam(const char *name)
    {
        if (nextParam == numParams)
            failx("Too many parameters supplied: No matching ? for parameter %s", name);
        return nextParam++;
    }
    inline void checkBind(CassError rc, const char *name)
    {
        if (rc != CASS_OK)
        {
            failx("While binding parameter %s: %s", name, cass_error_desc(rc));
        }
    }
    Owned<CassandraCluster> cluster;
    Owned<CassandraSession> session;
    Owned<CassandraStatementInfo> stmtInfo;
    Owned<CassandraDatasetBinder> inputStream;
    const IContextLogger &logctx;
    unsigned flags;
    unsigned nextParam;
    unsigned numParams;
    StringAttr queryString;

};

class CassandraEmbedContext : public CInterfaceOf<IEmbedContext>
{
public:
    virtual IEmbedFunctionContext *createFunctionContext(unsigned flags, const char *options)
    {
        return createFunctionContextEx(NULL, flags, options);
    }
    virtual IEmbedFunctionContext *createFunctionContextEx(ICodeContext * ctx, unsigned flags, const char *options)
    {
        if (flags & EFimport)
            UNSUPPORTED("IMPORT");
        else
            return new CassandraEmbedFunctionContext(ctx ? ctx->queryContextLogger() : queryDummyContextLogger(), flags, options);
    }
};

extern IEmbedContext* getEmbedContext()
{
    return new CassandraEmbedContext();
}

extern bool syntaxCheck(const char *script)
{
    return true; // MORE
}

//--------------------------------------------

#define ATTRIBUTES_NAME "attributes"

void addElement(IPTree *parent, const char *name, const CassValue *value)
{
    switch (cass_value_type(value))
    {
    case CASS_VALUE_TYPE_UNKNOWN:
        // It's a NULL - ignore it (or we could add empty element...)
        break;

    case CASS_VALUE_TYPE_ASCII:
    case CASS_VALUE_TYPE_TEXT:
    case CASS_VALUE_TYPE_VARCHAR:
    {
        rtlDataAttr str;
        unsigned chars;
        getUTF8Result(NULL, value, chars, str.refstr());
        StringAttr s(str.getstr(), rtlUtf8Size(chars, str.getstr()));
        parent->addProp(name, s);
        break;
    }

    case CASS_VALUE_TYPE_INT:
    case CASS_VALUE_TYPE_BIGINT:
    case CASS_VALUE_TYPE_VARINT:
        parent->addPropInt64(name, getSignedResult(NULL, value));
        break;

    case CASS_VALUE_TYPE_BLOB:
    {
        rtlDataAttr data;
        unsigned bytes;
        getDataResult(NULL, value, bytes, data.refdata());
        parent->addPropBin(name, bytes, data.getbytes());
        break;
    }
    case CASS_VALUE_TYPE_BOOLEAN:
        parent->addPropBool(name, getBooleanResult(NULL, value));
        break;

    case CASS_VALUE_TYPE_DOUBLE:
    case CASS_VALUE_TYPE_FLOAT:
    {
        double v = getRealResult(NULL, value);
        StringBuffer s;
        s.append(v);
        parent->addProp(name, s);
        break;
    }
    case CASS_VALUE_TYPE_LIST:
    case CASS_VALUE_TYPE_SET:
    {
        CassandraIterator elems(cass_iterator_from_collection(value));
        Owned<IPTree> list = createPTree(name);
        while (cass_iterator_next(elems))
            addElement(list, "item", cass_iterator_get_value(elems));
        parent->addPropTree(name, list.getClear());
        break;
    }
    case CASS_VALUE_TYPE_MAP:
    {
        CassandraIterator elems(cass_iterator_from_map(value));
        if (strcmp(name, ATTRIBUTES_NAME)==0 && isString(cass_value_primary_sub_type(value)))
        {
            while (cass_iterator_next(elems))
            {
                rtlDataAttr str;
                unsigned chars;
                getStringResult(NULL, cass_iterator_get_map_key(elems), chars, str.refstr());
                StringBuffer s("@");
                s.append(chars, str.getstr());
                addElement(parent, s, cass_iterator_get_map_value(elems));
            }
        }
        else
        {
            Owned<IPTree> map = createPTree(name);
            while (cass_iterator_next(elems))
            {
                if (isString(cass_value_primary_sub_type(value)))
                {
                    rtlDataAttr str;
                    unsigned chars;
                    getStringResult(NULL, cass_iterator_get_map_key(elems), chars, str.refstr());
                    StringAttr s(str.getstr(), chars);
                    addElement(map, s, cass_iterator_get_map_value(elems));
                }
                else
                {
                    Owned<IPTree> mapping = createPTree("mapping");
                    addElement(mapping, "key", cass_iterator_get_map_key(elems));
                    addElement(mapping, "value", cass_iterator_get_map_value(elems));
                    map->addPropTree("mapping", mapping.getClear());
                }
            }
            parent->addPropTree(name, map.getClear());
        }
        break;
    }
    default:
        DBGLOG("Column type %d not supported", cass_value_type(value));
        UNSUPPORTED("Column type");
    }
}

void bindElement(CassStatement *statement, IPTree *parent, unsigned idx, const char *name, CassValueType type)
{
    if (parent->hasProp(name) || strcmp(name, ATTRIBUTES_NAME)==0)
    {
        switch (type)
        {
        case CASS_VALUE_TYPE_ASCII:
        case CASS_VALUE_TYPE_TEXT:
        case CASS_VALUE_TYPE_VARCHAR:
        {
            const char *value = parent->queryProp(name);
            if (value)
                check(cass_statement_bind_string(statement, idx, cass_string_init(value)));
            break;
        }

        case CASS_VALUE_TYPE_INT:
            check(cass_statement_bind_int32(statement, idx, parent->getPropInt(name)));
            break;
        case CASS_VALUE_TYPE_BIGINT:
        case CASS_VALUE_TYPE_VARINT:
            check(cass_statement_bind_int64(statement, idx, parent->getPropInt64(name)));
            break;

        case CASS_VALUE_TYPE_BLOB:
        {
            MemoryBuffer buf;
            parent->getPropBin(name, buf);
            check(cass_statement_bind_bytes(statement, idx, cass_bytes_init((const cass_byte_t*)buf.toByteArray(), buf.length())));
            break;
        }
        case CASS_VALUE_TYPE_BOOLEAN:
            check(cass_statement_bind_bool(statement, idx, (cass_bool_t) parent->getPropBool(name)));
            break;

        case CASS_VALUE_TYPE_DOUBLE:
            check(cass_statement_bind_double(statement, idx, atof(parent->queryProp(name))));
            break;
        case CASS_VALUE_TYPE_FLOAT:
            check(cass_statement_bind_float(statement, idx, atof(parent->queryProp(name))));
            break;
        case CASS_VALUE_TYPE_LIST:
        case CASS_VALUE_TYPE_SET:
        {
            Owned<IPTree> child = parent->getPropTree(name);
            unsigned numItems = child->getCount("item");
            if (numItems)
            {
                CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_SET, numItems));
                Owned<IPTreeIterator> items = child->getElements("item");
                ForEach(*items)
                {
                    // We don't know the subtypes - we can assert that we only support string, for most purposes, I suspect
                    if (strcmp(name, "list1")==0)
                        check(cass_collection_append_int32(collection, items->query().getPropInt(NULL)));
                    else
                        check(cass_collection_append_string(collection, cass_string_init(items->query().queryProp(NULL))));
                }
                check(cass_statement_bind_collection(statement, idx, collection));
            }
            break;
        }

        case CASS_VALUE_TYPE_MAP:
        {
            // We don't know the subtypes - we can assert that we only support string, for most purposes, I suspect
            if (strcmp(name, ATTRIBUTES_NAME)==0)
            {
                Owned<IAttributeIterator> attrs = parent->getAttributes();
                unsigned numItems = attrs->count();
                ForEach(*attrs)
                {
                    numItems++;
                }
                if (numItems)
                {
                    CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_MAP, numItems));
                    ForEach(*attrs)
                    {
                        const char *key = attrs->queryName();
                        const char *value = attrs->queryValue();
                        check(cass_collection_append_string(collection, cass_string_init(key+1)));  // skip the @
                        check(cass_collection_append_string(collection, cass_string_init(value)));
                    }
                    check(cass_statement_bind_collection(statement, idx, collection));
                }
            }
            else
            {
                Owned<IPTree> child = parent->getPropTree(name);
                unsigned numItems = child->numChildren();
                // MORE - if the cassandra driver objects to there being fewer than numItems supplied, we may need to recode using a second pass.
                if (numItems)
                {
                    CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_MAP, numItems));
                    Owned<IPTreeIterator> items = child->getElements("*");
                    ForEach(*items)
                    {
                        IPTree &item = items->query();
                        const char *key = item.queryName();
                        const char *value = item.queryProp(NULL);
                        if (key && value)
                        {
                            check(cass_collection_append_string(collection, cass_string_init(key)));
                            check(cass_collection_append_string(collection, cass_string_init(value)));
                        }
                    }
                    check(cass_statement_bind_collection(statement, idx, collection));
                }
            }
            break;
        }
        default:
            DBGLOG("Column type %d not supported", type);
            UNSUPPORTED("Column type");
        }
    }
}


extern void cassandraToGenericXML()
{
    CassandraCluster cluster(cass_cluster_new());
    cass_cluster_set_contact_points(cluster, "127.0.0.1");

    CassandraSession session(cass_session_new());
    CassandraFuture future(cass_session_connect_keyspace(session, cluster, "test"));
    future.wait("connect");
    CassandraStatement statement(cass_statement_new(cass_string_init("select * from tbl1 where name = 'name1';"), 0));
    CassandraFuture future2(cass_session_execute(session, statement));
    future2.wait("execute");
    CassandraResult result(cass_future_get_result(future2));
    StringArray names;
    UnsignedArray types;
    for (int i = 0; i < cass_result_column_count(result); i++)
    {
        CassString column = cass_result_column_name(result, i);
        StringBuffer name(column.length, column.data);
        names.append(name);
        types.append(cass_result_column_type(result, i));
    }
    // Now fetch the rows
    Owned<IPTree> xml = createPTree("tbl1");
    CassandraIterator rows(cass_iterator_from_result(result));
    while (cass_iterator_next(rows))
    {
        CassandraIterator cols(cass_iterator_from_row(cass_iterator_get_row(rows)));
        Owned<IPTree> row = createPTree("row");
        unsigned colidx = 0;
        while (cass_iterator_next(cols))
        {
            const CassValue *value = cass_iterator_get_column(cols);
            const char *name = names.item(colidx);
            addElement(row, name, value);
            colidx++;
        }
        xml->addPropTree("row", row.getClear());
    }
    xml->setProp("row[1]/name", "newname");
    StringBuffer buf;
    toXML(xml, buf);
    DBGLOG("%s", buf.str());

    // Now try going the other way...
    // For this we need to know the expected names (can fetch them from system table) and types (ditto, potentially, though a dummy select may be easier)
    StringBuffer colNames;
    StringBuffer values;
    ForEachItemIn(idx, names)
    {
        colNames.append(",").append(names.item(idx));
        values.append(",?");
    }
    VStringBuffer insertQuery("INSERT into tbl1 (%s) values (%s);", colNames.str()+1, values.str()+1);
    Owned<IPTreeIterator> xmlRows = xml->getElements("row");
    ForEach(*xmlRows)
    {
        IPropertyTree *xmlrow = &xmlRows->query();
        CassandraStatement update(cass_statement_new(cass_string_init(insertQuery.str()), names.length()));
        ForEachItemIn(idx, names)
        {
            bindElement(update, xmlrow, idx, names.item(idx), (CassValueType) types.item(idx));
        }
        // MORE - use a batch
        CassandraFuture future3(cass_session_execute(session, update));
        future2.wait("insert");
    }

}

//--------------------------------------------

interface ICassandraSession
{
    virtual CassSession *querySession() const = 0;
    virtual CassandraPrepared *prepareStatement(const char *query) const = 0;
    virtual unsigned queryTraceLevel() const = 0;
};


struct CassandraColumnMapper
{
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value) = 0;
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal = 0) = 0;
};

class StringColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        rtlDataAttr str;
        unsigned chars;
        getUTF8Result(NULL, value, chars, str.refstr());
        StringAttr s(str.getstr(), rtlUtf8Size(chars, str.getstr()));
        row->setProp(name, s);
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        const char *value = row->queryProp(name);
        if (!value)
            return false;
        if (statement)
            check(cass_statement_bind_string(statement, idx, cass_string_init(value)));
        return true;
    }
} stringColumnMapper;

class RequiredStringColumnMapper : public StringColumnMapper
{
public:
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        const char *value = row->queryProp(name);
        if (!value)
            value = "";
        if (statement)
            check(cass_statement_bind_string(statement, idx, cass_string_init(value)));
        return true;
    }
} requiredStringColumnMapper;

class BlobColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        rtlDataAttr str;
        unsigned chars;
        getDataResult(NULL, value, chars, str.refdata());
        row->setPropBin(name, chars, str.getbytes());
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        MemoryBuffer value;
        row->getPropBin(name, value);
        if (value.length())
        {
            if (statement)
                check(cass_statement_bind_bytes(statement, idx, cass_bytes_init((const cass_byte_t *) value.toByteArray(), value.length())));
            return true;
        }
        else
            return false;
    }
} blobColumnMapper;

class TimeStampColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        // never fetched (that may change?)
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        // never bound
        return false;
    }
} timestampColumnMapper;

class RootNameColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        rtlDataAttr str;
        unsigned chars;
        getUTF8Result(NULL, value, chars, str.refstr());
        StringAttr s(str.getstr(), rtlUtf8Size(chars, str.getstr()));
        row->renameProp("/", s);
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        if (statement)
        {
            const char *value = row->queryName();
            check(cass_statement_bind_string(statement, idx, cass_string_init(value)));
        }
        return true;
    }
} rootNameColumnMapper;

// WuidColumnMapper is used for columns containing a wuid that is NOT in the resulting XML - it
// is an error to try to map such a column to/from the XML representation

class WuidColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        throwUnexpected();
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        throwUnexpected();
    }
} wuidColumnMapper;

class GraphIdColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        rtlDataAttr str;
        unsigned chars;
        getUTF8Result(NULL, value, chars, str.refstr());
        StringAttr s(str.getstr(), rtlUtf8Size(chars, str.getstr()));
        if (strcmp(s, "Running")==0)  // The input XML structure is a little odd
            return row;
        else
        {
            if (!row->hasProp(s))
                row->addPropTree(s, createPTree());
            return row->queryPropTree(s);
        }
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        const char *value = row->queryName();
        if (!value)
            return false;
        if (statement)
            check(cass_statement_bind_string(statement, idx, cass_string_init(value)));
        return true;
    }
} graphIdColumnMapper;

class ProgressColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        rtlDataAttr str;
        unsigned chars;
        getDataResult(NULL, value, chars, str.refdata());  // Stored as a blob in case we want to compress
        IPTree *child = createPTreeFromXMLString(chars, str.getstr());  // For now, assume we did not compress!
        row->addPropTree(child->queryName(), child);
        return child;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        // MORE - may need to read, and probably should write, compressed.
        StringBuffer value;
        ::toXML(row, value, 0, 0);
        if (value.length())
        {
            if (statement)
                check(cass_statement_bind_bytes(statement, idx, cass_bytes_init((const cass_byte_t *) value.str(), value.length())));
            return true;
        }
        else
            return false;
    }
} progressColumnMapper;

class BoolColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        row->addPropBool(name, getBooleanResult(NULL, value));
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        if (row->hasProp(name))
        {
            if (statement)
            {
                bool value = row->getPropBool(name, false);
                check(cass_statement_bind_bool(statement, idx, value ? cass_true : cass_false));
            }
            return true;
        }
        else
            return false;
    }
} boolColumnMapper;

class IntColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        row->addPropInt(name, getSignedResult(NULL, value));
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        if (row->hasProp(name))
        {
            if (statement)
            {
                int value = row->getPropInt(name);
                check(cass_statement_bind_int32(statement, idx, value));
            }
            return true;
        }
        else
            return false;
    }
} intColumnMapper;

class DefaultedIntColumnMapper : public IntColumnMapper
{
public:
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int defaultValue)
    {
        if (statement)
        {
            int value = row->getPropInt(name, defaultValue);
            check(cass_statement_bind_int32(statement, idx, value));
        }
        return true;
    }
} defaultedIntColumnMapper;

class BigIntColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        row->addPropInt64(name, getSignedResult(NULL, value));
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        if (row->hasProp(name))
        {
            if (statement)
            {
                __int64 value = row->getPropInt64(name);
                check(cass_statement_bind_int64(statement, idx, value));
            }
            return true;
        }
        else
            return false;
    }
} bigintColumnMapper;

class SubgraphIdColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        __int64 id = getSignedResult(NULL, value);
        if (id)
            row->addPropInt64(name, id);
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        if (statement)
        {
            int value = row->getPropInt(name);
            check(cass_statement_bind_int64(statement, idx, value));
        }
        return true;
    }
} subgraphIdColumnMapper;

class SimpleMapColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        Owned<IPTree> map = createPTree(name);
        CassandraIterator elems(cass_iterator_from_map(value));
        while (cass_iterator_next(elems))
        {
            rtlDataAttr str;
            unsigned chars;
            getStringResult(NULL, cass_iterator_get_map_key(elems), chars, str.refstr());
            StringAttr s(str.getstr(), chars);
            stringColumnMapper.toXML(map, s, cass_iterator_get_map_value(elems));
        }
        row->addPropTree(name, map.getClear());
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        Owned<IPTree> child = row->getPropTree(name);
        if (child)
        {
            unsigned numItems = child->numChildren();
            if (numItems)
            {
                if (statement)
                {
                    CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_MAP, numItems));
                    Owned<IPTreeIterator> items = child->getElements("*");
                    ForEach(*items)
                    {
                        IPTree &item = items->query();
                        const char *key = item.queryName();
                        const char *value = item.queryProp(NULL);
                        if (key && value)
                        {
                            check(cass_collection_append_string(collection, cass_string_init(key)));
                            check(cass_collection_append_string(collection, cass_string_init(value)));
                        }
                    }
                    check(cass_statement_bind_collection(statement, idx, collection));
                }
                return true;
            }
        }
        return false;
    }
} simpleMapColumnMapper;

class AttributeMapColumnMapper : implements CassandraColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        CassandraIterator elems(cass_iterator_from_map(value));
        while (cass_iterator_next(elems))
        {
            rtlDataAttr str;
            unsigned chars;
            getStringResult(NULL, cass_iterator_get_map_key(elems), chars, str.refstr());
            StringBuffer s("@");
            s.append(chars, str.getstr());
            stringColumnMapper.toXML(row, s, cass_iterator_get_map_value(elems));
        }
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        // NOTE - name here provides a list of attributes that we should NOT be mapping
        Owned<IAttributeIterator> attrs = row->getAttributes();
        unsigned numItems = 0;
        ForEach(*attrs)
        {
            StringBuffer key = attrs->queryName();
            key.append('@');
            if (strstr(name, key) == NULL)
                numItems++;
        }
        if (numItems)
        {
            if (statement)
            {
                CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_MAP, numItems));
                ForEach(*attrs)
                {
                    StringBuffer key = attrs->queryName();
                    key.append('@');
                    if (strstr(name, key) == NULL)
                    {
                        const char *value = attrs->queryValue();
                        check(cass_collection_append_string(collection, cass_string_init(attrs->queryName()+1)));  // skip the @
                        check(cass_collection_append_string(collection, cass_string_init(value)));
                    }
                }
                check(cass_statement_bind_collection(statement, idx, collection));
            }
            return true;
        }
        else
            return false;
    }
} attributeMapColumnMapper;

class QueryTextColumnMapper : public StringColumnMapper
{
public:
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        // Name is "Query/Text ...
        IPTree *query = row->queryPropTree("Query");
        if (!query)
        {
            query = createPTree("Query");
            row->setPropTree("Query", query);
            row->setProp("Query/@fetchEntire", "1"); // Compatibility...
        }
        return StringColumnMapper::toXML(query, "Text", value);
    }
} queryTextColumnMapper;

class GraphMapColumnMapper : implements CassandraColumnMapper
{
public:
    GraphMapColumnMapper(const char *_elemName, const char *_nameAttr)
    : elemName(_elemName), nameAttr(_nameAttr)
    {
    }
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        Owned<IPTree> map = createPTree(name);
        CassandraIterator elems(cass_iterator_from_map(value));
        while (cass_iterator_next(elems))
        {
            rtlDataAttr str;
            unsigned chars;
            getStringResult(NULL, cass_iterator_get_map_value(elems), chars, str.refstr());
            Owned<IPTree> child = createPTreeFromXMLString(chars, str.getstr());
            map->addPropTree(elemName, child.getClear());
        }
        row->addPropTree(name, map.getClear());
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        Owned<IPTree> child = row->getPropTree(name);
        if (child)
        {
            unsigned numItems = child->numChildren();
            if (numItems)
            {
                if (statement)
                {
                    CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_MAP, numItems));
                    Owned<IPTreeIterator> items = child->getElements("*");
                    ForEach(*items)
                    {
                        IPTree &item = items->query();
                        const char *key = item.queryProp(nameAttr);
                        // MORE - may need to read, and probably should write, compressed. At least for graphs
                        StringBuffer value;
                        ::toXML(&item, value, 0, 0);
                        if (key && value.length())
                        {
                            check(cass_collection_append_string(collection, cass_string_init(key)));
                            check(cass_collection_append_string(collection, cass_string_init(value)));
                        }
                    }
                    check(cass_statement_bind_collection(statement, idx, collection));
                }
                return true;
            }
        }
        return false;
    }
private:
    const char *elemName;
    const char *nameAttr;
} graphMapColumnMapper("Graph", "@name"), workflowMapColumnMapper("Item", "@wfid");

class AssociationsMapColumnMapper : public GraphMapColumnMapper
{
public:
    AssociationsMapColumnMapper(const char *_elemName, const char *_nameAttr)
    : GraphMapColumnMapper(_elemName, _nameAttr)
    {
    }
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        // Name is "Query/Associated ...
        IPTree *query = row->queryPropTree("Query");
        if (!query)
        {
            query = createPTree("Query");
            row->setPropTree("Query", query);
            row->setProp("Query/@fetchEntire", "1"); // Compatibility...
        }
        return GraphMapColumnMapper::toXML(query, "Associated", value);
    }
} associationsMapColumnMapper("File", "@filename");

class PluginListColumnMapper : implements CassandraColumnMapper
{
public:
    PluginListColumnMapper(const char *_elemName, const char *_nameAttr)
    : elemName(_elemName), nameAttr(_nameAttr)
    {
    }
    virtual IPTree *toXML(IPTree *row, const char *name, const CassValue *value)
    {
        Owned<IPTree> map = createPTree(name);
        CassandraIterator elems(cass_iterator_from_collection(value));
        while (cass_iterator_next(elems))
        {
            Owned<IPTree> child = createPTree(elemName);
            stringColumnMapper.toXML(child, nameAttr, cass_iterator_get_value(elems));
            map->addPropTree(elemName, child.getClear());
        }
        row->addPropTree(name, map.getClear());
        return row;
    }
    virtual bool fromXML(CassStatement *statement, unsigned idx, IPTree *row, const char *name, int userVal)
    {
        Owned<IPTree> child = row->getPropTree(name);
        if (child)
        {
            unsigned numItems = child->numChildren();
            if (numItems)
            {
                if (statement)
                {
                    CassandraCollection collection(cass_collection_new(CASS_COLLECTION_TYPE_LIST, numItems));
                    Owned<IPTreeIterator> items = child->getElements("*");
                    ForEach(*items)
                    {
                        IPTree &item = items->query();
                        const char *value = item.queryProp(nameAttr);
                        if (value)
                            check(cass_collection_append_string(collection, cass_string_init(value)));
                    }
                    check(cass_statement_bind_collection(statement, idx, collection));
                }
                return true;
            }
        }
        return false;
    }
private:
    const char *elemName;
    const char *nameAttr;
} pluginListColumnMapper("Plugin", "@dllname");

struct CassandraXmlMapping
{
    const char *columnName;
    const char *columnType;
    const char *xpath;
    CassandraColumnMapper &mapper;
};

struct CassandraTableInfo
{
    const char *x;
    const CassandraXmlMapping *mappings;
};

const CassandraXmlMapping workunitsMappings [] =
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"clustername", "text", "@clusterName", stringColumnMapper},
    {"jobname", "text", "@jobName", stringColumnMapper},
    {"priorityclass", "int", "@priorityClass", intColumnMapper},
    {"protected", "boolean", "@protected", boolColumnMapper},
    {"scope", "text", "@scope", stringColumnMapper},
    {"submitID", "text", "@submitID", stringColumnMapper},
    {"state", "text", "@state", stringColumnMapper},

    {"debug", "map<text, text>", "Debug", simpleMapColumnMapper},
    {"attributes", "map<text, text>", "@wuid@clusterName@jobName@priorityClass@protected@scope@submitID@state@", attributeMapColumnMapper},  // name is the suppression list, note trailing @
    {"graphs", "map<text, text>", "Graphs", graphMapColumnMapper}, // MORE - make me lazy...
    {"plugins", "list<text>", "Plugins", pluginListColumnMapper},
    {"query", "text", "Query/Text", queryTextColumnMapper},        // MORE - make me lazy...
    {"associations", "map<text, text>", "Query/Associated", associationsMapColumnMapper},
    {"workflow", "map<text, text>", "Workflow", workflowMapColumnMapper},
    { NULL, "workunits", "((wuid))", stringColumnMapper}
};

const CassandraXmlMapping workunitInfoMappings [] =  // A cut down version of the workunit mappings - used when querying with no key
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"clustername", "text", "@clusterName", stringColumnMapper},
    {"jobname", "text", "@jobName", stringColumnMapper},
    {"priorityclass", "int", "@priorityClass", intColumnMapper},
    {"protected", "boolean", "@protected", boolColumnMapper},
    {"scope", "text", "@scope", stringColumnMapper},
    {"submitID", "text", "@submitID", stringColumnMapper},
    {"state", "text", "@state", stringColumnMapper},
    { NULL, "workunits", "((wuid))", stringColumnMapper}
};

// The following describe secondary tables - they contain copies of the basic wu information but keyed by different fields

const CassandraXmlMapping ownerMappings [] =
{
    {"submitID", "text", "@submitID", stringColumnMapper},
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"clustername", "text", "@clusterName", stringColumnMapper},
    {"jobname", "text", "@jobName", stringColumnMapper},
    {"priorityclass", "int", "@priorityClass", intColumnMapper},
    {"protected", "boolean", "@protected", boolColumnMapper},
    {"scope", "text", "@scope", stringColumnMapper},
    {"state", "text", "@state", stringColumnMapper},
    { NULL, "workunitsByOwner", "((submitID), wuid)", stringColumnMapper}
};

const CassandraXmlMapping clusterMappings [] =
{
    {"clustername", "text", "@clusterName", stringColumnMapper},
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"submitID", "text", "@submitID", stringColumnMapper},
    {"jobname", "text", "@jobName", stringColumnMapper},
    {"priorityclass", "int", "@priorityClass", intColumnMapper},
    {"protected", "boolean", "@protected", boolColumnMapper},
    {"scope", "text", "@scope", stringColumnMapper},
    {"state", "text", "@state", stringColumnMapper},
    { NULL, "workunitsByCluster", "((clustername), wuid)", stringColumnMapper}
};

const CassandraXmlMapping jobnameMappings [] =
{
    {"jobname", "text", "@jobName", stringColumnMapper},
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"submitID", "text", "@submitID", stringColumnMapper},
    {"clustername", "text", "@clusterName", stringColumnMapper},
    {"priorityclass", "int", "@priorityClass", intColumnMapper},
    {"protected", "boolean", "@protected", boolColumnMapper},
    {"scope", "text", "@scope", stringColumnMapper},
    {"state", "text", "@state", stringColumnMapper},
    { NULL, "workunitsByJobname", "((jobname), wuid)", stringColumnMapper}
};

const CassandraXmlMapping * secondaryTables [] = { ownerMappings, clusterMappings, jobnameMappings, NULL };

// The following describe child tables - all keyed by wuid

enum ChildTablesEnum { WuExceptionsChild, WuStatisticsChild, WuGraphProgressChild, WuResultsChild, WuVariablesChild, ChildTablesSize };

struct ChildTableInfo
{
    const char *parentElement;
    const char *childElement;
    ChildTablesEnum index;
    const CassandraXmlMapping *mappings;
};

const CassandraXmlMapping wuExceptionsMappings [] =
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"attributes", "map<text, text>", "", attributeMapColumnMapper},
    {"value", "text", ".", stringColumnMapper},
    {"ts", "timeuuid", NULL, timestampColumnMapper}, // must be last since we don't bind it, so it would throw out the colidx values of following fields
    { NULL, "wuExceptions", "((wuid), ts)", stringColumnMapper}
};

const ChildTableInfo wuExceptionsTable =
{
        "Exceptions", "Exception",
        WuExceptionsChild,
        wuExceptionsMappings
};

const CassandraXmlMapping wuStatisticsMappings [] =
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"ts", "bigint", "@ts", bigintColumnMapper},  // MORE - should change this to a timeuuid ?
    {"kind", "text", "@kind", stringColumnMapper},
    {"creator", "text", "@creator", stringColumnMapper},
    {"scope", "text", "@scope", stringColumnMapper},
    {"attributes", "map<text, text>", "@ts@kind@creator@scope@", attributeMapColumnMapper},
    { NULL, "wuStatistics", "((wuid), ts, kind, creator, scope)", stringColumnMapper}
};

const ChildTableInfo wuStatisticsTable =
{
        "Statistics", "Statistic",
        WuStatisticsChild,
        wuStatisticsMappings
};

const CassandraXmlMapping wuGraphProgressMappings [] =
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"graphID", "text", NULL, graphIdColumnMapper},
    {"progress", "blob", NULL, progressColumnMapper},  // NOTE - order of these is significant - this creates the subtree that ones below will modify
    {"subgraphID", "text", "@id", subgraphIdColumnMapper},
    {"state", "int", "@_state", intColumnMapper},
    { NULL, "wuGraphProgress", "((wuid), graphid, subgraphid)", stringColumnMapper}
};

const ChildTableInfo wuGraphProgressTable =
{
        "Bit of a", "Special case",
        WuGraphProgressChild,
        wuGraphProgressMappings
};

const CassandraXmlMapping wuResultsMappings [] =
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"sequence", "int", "@sequence", intColumnMapper},
    {"name", "text", "@name", stringColumnMapper},
    {"format", "text", "@format", stringColumnMapper},  // xml, xmlset, csv, or null to mean raw. Could probably switch to int if we wanted
    {"status", "text", "@status", stringColumnMapper},
    {"attributes", "map<text, text>", "@sequence@name@format@status@", attributeMapColumnMapper},  // name is the suppression list. We could consider folding format/status into this?
    {"rowcount", "int", "rowCount", intColumnMapper}, // This is the number of rows in result (which may be stored in a file rather than in value)
    {"totalrowcount", "bigint", "totalRowCount", bigintColumnMapper},// This is the number of rows in value
    {"schemaRaw", "blob", "SchemaRaw", blobColumnMapper},
    {"logicalName", "text", "logicalName", stringColumnMapper},  // either this or value will be present once result status is "calculated"
    {"value", "blob", "Value", blobColumnMapper},
    { NULL, "wuResults", "((wuid), sequence)", stringColumnMapper}
};

const ChildTableInfo wuResultsTable =
{
        "Results", "Result",
        WuResultsChild,
        wuResultsMappings
};

// This looks very similar to the above, but the key is different...

const CassandraXmlMapping wuVariablesMappings [] =
{
    {"wuid", "text", NULL, rootNameColumnMapper},
    {"sequence", "int", "@sequence", defaultedIntColumnMapper},  // Note - should be either variable or temporary...
    {"name", "text", "@name", requiredStringColumnMapper},
    {"format", "text", "@format", stringColumnMapper},  // xml, xmlset, csv, or null to mean raw. Could probably switch to int if we wanted
    {"status", "text", "@status", stringColumnMapper},
    {"rowcount", "int", "rowCount", intColumnMapper}, // This is the number of rows in result (which may be stored in a file rather than in value)
    {"totalrowcount", "bigint", "totalRowCount", bigintColumnMapper},// This is the number of rows in value
    {"schemaRaw", "blob", "SchemaRaw", blobColumnMapper},
    {"logicalName", "text", "logicalName", stringColumnMapper},  // either this or value will be present once result status is "calculated"
    {"value", "blob", "Value", blobColumnMapper},
    { NULL, "wuVariables", "((wuid), sequence, name)", stringColumnMapper}
};

const ChildTableInfo wuVariablesTable =
{
        "Variables", "Variable", // Actually sometimes uses Variables, sometimes Temporaries.... MORE - think about how to fix that...
        WuVariablesChild,
        wuVariablesMappings
};

// Order should match the enum above
const ChildTableInfo * childTables [] = { &wuExceptionsTable, &wuStatisticsTable, &wuGraphProgressTable, &wuResultsTable, &wuVariablesTable, NULL };

void getBoundFieldNames(const CassandraXmlMapping *mappings, StringBuffer &names, StringBuffer &bindings, IPTree *inXML, StringBuffer &tableName)
{
    while (mappings->columnName)
    {
        if (mappings->mapper.fromXML(NULL, 0, inXML, mappings->xpath))
        {
            names.appendf(",%s", mappings->columnName);
            if (strcmp(mappings->columnType, "timeuuid")==0)
                bindings.appendf(",now()");
            else
                bindings.appendf(",?");
        }
        mappings++;
    }
    tableName.append(mappings->columnType);
}

void getFieldNames(const CassandraXmlMapping *mappings, StringBuffer &names, StringBuffer &tableName)
{
    while (mappings->columnName)
    {
        names.appendf(",%s", mappings->columnName);
        mappings++;
    }
    tableName.append(mappings->columnType);
}

const char *queryTableName(const CassandraXmlMapping *mappings)
{
    while (mappings->columnName)
        mappings++;
    return mappings->columnType;
}

StringBuffer & describeTable(const CassandraXmlMapping *mappings, StringBuffer &out)
{
    StringBuffer fields;
    while (mappings->columnName)
    {
        fields.appendf("%s %s,", mappings->columnName, mappings->columnType);
        mappings++;
    }
    return out.appendf("CREATE TABLE IF NOT EXISTS %s (%s PRIMARY KEY %s);", mappings->columnType, fields.str(), mappings->xpath);
}

const CassResult *executeQuery(CassSession *session, CassStatement *statement)
{
    CassandraFuture future(cass_session_execute(session, statement));
    future.wait("executeQuery");
    return cass_future_get_result(future);
}

const CassResult *fetchDataForKey(const char *key, CassSession *session, const CassandraXmlMapping *mappings)
{
    StringBuffer names;
    StringBuffer tableName;
    getFieldNames(mappings+(key?1:0), names, tableName);  // mappings+1 means we don't return the key column
    VStringBuffer selectQuery("select %s from %s", names.str()+1, tableName.str());
    if (key)
        selectQuery.appendf(" where %s='%s'", mappings->columnName, key); // MORE - should consider using prepared for this - is it faster?
    selectQuery.append(';');
    //if (traceLevel >= 2)
    //    DBGLOG("%s", selectQuery.str());
    CassandraStatement statement(cass_statement_new(cass_string_init(selectQuery.str()), 0));
    return executeQuery(session, statement);
}

const CassResult *fetchDataForKeyAndWuid(const char *key, const char *wuid, CassSession *session, const CassandraXmlMapping *mappings)
{
    StringBuffer names;
    StringBuffer tableName;
    getFieldNames(mappings+2, names, tableName);  // mappings+1 means we don't return the key column
    VStringBuffer selectQuery("select %s from %s where %s='%s' and wuid='%s'", names.str()+1, tableName.str(), mappings->columnName, key, wuid); // MORE - should consider using prepared/bind for this - is it faster?
    selectQuery.append(';');
    //if (traceLevel >= 2)
    //    DBGLOG("%s", selectQuery.str());
    CassandraStatement statement(cass_statement_new(cass_string_init(selectQuery.str()), 0));
    return executeQuery(session, statement);
}

void deleteSecondaryByKey(const CassandraXmlMapping *mappings, const char *wuid, const char *key, const ICassandraSession *sessionCache, CassBatch *batch)
{
    if (key && *key)
    {
        StringBuffer names;
        StringBuffer tableName;
        getFieldNames(mappings, names, tableName);
        VStringBuffer insertQuery("DELETE from %s where %s=? and wuid=?;", tableName.str(), mappings[0].columnName);
        Owned<CassandraPrepared> prepared = sessionCache->prepareStatement(insertQuery);
        CassandraStatement update(cass_prepared_bind(*prepared));
        check(cass_statement_bind_string(update, 0, cass_string_init(key)));
        check(cass_statement_bind_string(update, 1, cass_string_init(wuid)));
        check(cass_batch_add_statement(batch, update));
    }
}

void deleteChildByWuid(const CassandraXmlMapping *mappings, const char *wuid, const ICassandraSession *sessionCache, CassBatch *batch)
{
    StringBuffer names;
    StringBuffer tableName;
    getFieldNames(mappings, names, tableName);
    VStringBuffer insertQuery("DELETE from %s where wuid=?;", tableName.str());
    Owned<CassandraPrepared> prepared = sessionCache->prepareStatement(insertQuery);
    CassandraStatement update(cass_prepared_bind(*prepared));
    check(cass_statement_bind_string(update, 0, cass_string_init(wuid)));
    check(cass_batch_add_statement(batch, update));
}

void executeSimpleCommand(CassSession *session, const char *command)
{
    CassandraStatement statement(cass_statement_new(cass_string_init(command), 0));
    CassandraFuture future(cass_session_execute(session, statement));
    future.wait("execute");
}

void ensureTable(CassSession *session, const CassandraXmlMapping *mappings)
{
    StringBuffer schema;
    executeSimpleCommand(session, describeTable(mappings, schema));
}

extern void simpleXMLtoCassandra(const ICassandraSession *session, CassBatch *batch, const CassandraXmlMapping *mappings, IPTree *inXML)
{
    StringBuffer names;
    StringBuffer bindings;
    StringBuffer tableName;
    getBoundFieldNames(mappings, names, bindings, inXML, tableName);
    VStringBuffer insertQuery("INSERT into %s (%s) values (%s);", tableName.str(), names.str()+1, bindings.str()+1);
    Owned<CassandraPrepared> prepared = session->prepareStatement(insertQuery);
    CassandraStatement update(cass_prepared_bind(*prepared));
    unsigned bindidx = 0;
    while (mappings->columnName)
    {
        if (mappings->mapper.fromXML(update, bindidx, inXML, mappings->xpath, 0))
            bindidx++;
        mappings++;
    }
    check(cass_batch_add_statement(batch, update));
}

extern void childXMLtoCassandra(const ICassandraSession *session, CassBatch *batch, const CassandraXmlMapping *mappings, const char *wuid, IPTreeIterator *elements, int defaultValue)
{
    if (elements->first())
    {
        do
        {
            IPTree &result = elements->query();
            StringBuffer bindings;
            StringBuffer names;
            StringBuffer tableName;
            getBoundFieldNames(mappings, names, bindings, &result, tableName);
            VStringBuffer insertQuery("INSERT into %s (%s) values (%s);", tableName.str(), names.str()+1, bindings.str()+1);
            Owned<CassandraPrepared> prepared = session->prepareStatement(insertQuery);
            CassandraStatement update(cass_prepared_bind(*prepared));
            check(cass_statement_bind_string(update, 0, cass_string_init(wuid)));
            unsigned bindidx = 1; // We already bound wuid
            unsigned colidx = 1; // We already bound wuid
            while (mappings[colidx].columnName)
            {
                if (mappings[colidx].mapper.fromXML(update, bindidx, &result, mappings[colidx].xpath, defaultValue))
                    bindidx++;
                colidx++;
            }
            check(cass_batch_add_statement(batch, update));
        }
        while (elements->next());
    }
}

extern void childXMLtoCassandra(const ICassandraSession *session, CassBatch *batch, const CassandraXmlMapping *mappings, IPTree *inXML, const char *xpath, int defaultValue)
{
    Owned<IPTreeIterator> elements = inXML->getElements(xpath);
    childXMLtoCassandra(session, batch, mappings, inXML->queryName(), elements, defaultValue);
}

extern void wuResultsXMLtoCassandra(const ICassandraSession *session, CassBatch *batch, IPTree *inXML, const char *xpath)
{
    childXMLtoCassandra(session, batch, wuResultsMappings, inXML, xpath, 0);
}

extern void wuVariablesXMLtoCassandra(const ICassandraSession *session, CassBatch *batch, IPTree *inXML, const char *xpath, int defaultSequence)
{
    childXMLtoCassandra(session, batch, wuVariablesMappings, inXML, xpath, defaultSequence);
}

extern void cassandraToWuChildXML(CassSession *session, const CassandraXmlMapping *mappings, const char *parentElement, const char *childElement, const char *wuid, IPTree *wuTree)
{
    CassandraResult result(fetchDataForKey(wuid, session, mappings));
    Owned<IPTree> results;
    CassandraIterator rows(cass_iterator_from_result(result));
    while (cass_iterator_next(rows))
    {
        CassandraIterator cols(cass_iterator_from_row(cass_iterator_get_row(rows)));
        Owned<IPTree> child;
        if (!results)
            results.setown(createPTree(parentElement));
        child.setown(createPTree(childElement));
        unsigned colidx = 1;  // We did not fetch wuid
        while (cass_iterator_next(cols))
        {
            assertex(mappings[colidx].columnName);
            const CassValue *value = cass_iterator_get_column(cols);
            if (value && !cass_value_is_null(value))
                mappings[colidx].mapper.toXML(child, mappings[colidx].xpath, value);
            colidx++;
        }
        const char *childName = child->queryName();
        results->addPropTree(childName, child.getClear());
    }
    if (results)
        wuTree->addPropTree(parentElement, results.getClear());
}

extern void cassandraToWuVariablesXML(CassSession *session, const char *wuid, IPTree *wuTree)
{
    CassandraResult result(fetchDataForKey(wuid, session, wuVariablesMappings));
    Owned<IPTree> variables;
    Owned<IPTree> temporaries;
    CassandraIterator rows(cass_iterator_from_result(result));
    while (cass_iterator_next(rows))
    {
        CassandraIterator cols(cass_iterator_from_row(cass_iterator_get_row(rows)));
        if (!cass_iterator_next(cols))
            fail("No column found reading wuvariables.sequence");
        const CassValue *sequenceValue = cass_iterator_get_column(cols);
        int sequence = getSignedResult(NULL, sequenceValue);
        Owned<IPTree> child;
        IPTree *parent;
        switch (sequence)
        {
        case ResultSequenceStored:
            if (!variables)
                variables.setown(createPTree("Variables"));
            child.setown(createPTree("Variable"));
            parent = variables;
            break;
        case ResultSequenceInternal:
        case ResultSequenceOnce:
            if (!temporaries)
                temporaries.setown(createPTree("Temporaries"));
            child.setown(createPTree("Variable"));
            parent = temporaries;
            break;
        default:
            throwUnexpected();
            break;
        }
        unsigned colidx = 2;
        while (cass_iterator_next(cols))
        {
            assertex(wuVariablesMappings[colidx].columnName);
            const CassValue *value = cass_iterator_get_column(cols);
            if (value && !cass_value_is_null(value))
                wuVariablesMappings[colidx].mapper.toXML(child, wuVariablesMappings[colidx].xpath, value);
            colidx++;
        }
        const char *childName = child->queryName();
        parent->addPropTree(childName, child.getClear());
    }
    if (variables)
        wuTree->addPropTree("Variables", variables.getClear());
    if (temporaries)
        wuTree->addPropTree("Temporaries", temporaries.getClear());
}
/*
extern void graphProgressXMLtoCassandra(CassSession *session, IPTree *inXML)
{
    StringBuffer names;
    StringBuffer bindings;
    StringBuffer tableName;
    int numBound = getFieldNames(graphProgressMappings, names, bindings, tableName);
    VStringBuffer insertQuery("INSERT into %s (%s) values (%s);", tableName.str(), names.str()+1, bindings.str()+1);
    DBGLOG("%s", insertQuery.str());
    CassandraBatch batch(cass_batch_new(CASS_BATCH_TYPE_UNLOGGED));
    CassandraFuture futurePrep(cass_session_prepare(session, cass_string_init(insertQuery)));
    futurePrep.wait("prepare statement");
    CassandraPrepared prepared(cass_future_get_prepared(futurePrep));

    Owned<IPTreeIterator> graphs = inXML->getElements("./graph*");
    ForEach(*graphs)
    {
        IPTree &graph = graphs->query();
        Owned<IPTreeIterator> subgraphs = graph.getElements("./node");
        ForEach(*subgraphs)
        {
            IPTree &subgraph = subgraphs->query();
            CassandraStatement update(cass_prepared_bind(prepared));
            graphProgressMappings[0].mapper.fromXML(update, 0, inXML, graphProgressMappings[0].xpath);
            graphProgressMappings[1].mapper.fromXML(update, 1, &graph, graphProgressMappings[1].xpath);
            unsigned colidx = 2;
            while (graphProgressMappings[colidx].columnName)
            {
                graphProgressMappings[colidx].mapper.fromXML(update, colidx, &subgraph, graphProgressMappings[colidx].xpath);
                colidx++;
            }
            check(cass_batch_add_statement(batch, update));
        }
        // And one more with subgraphid = 0 for the graph status
        CassandraStatement update(cass_statement_new(cass_string_init(insertQuery.str()), bindings.length()/2));
        graphProgressMappings[0].mapper.fromXML(update, 0, inXML, graphProgressMappings[0].xpath);
        graphProgressMappings[1].mapper.fromXML(update, 1, &graph, graphProgressMappings[1].xpath);
        check(cass_statement_bind_int64(update, 3, 0)); // subgraphId can't be null, as it's in the key
        unsigned colidx = 4;  // we skip progress and subgraphid
        while (graphProgressMappings[colidx].columnName)
        {
            graphProgressMappings[colidx].mapper.fromXML(update, colidx, &graph, graphProgressMappings[colidx].xpath);
            colidx++;
        }
        check(cass_batch_add_statement(batch, update));
    }
    if (inXML->hasProp("Running"))
    {
        IPTree *running = inXML->queryPropTree("Running");
        CassandraStatement update(cass_statement_new(cass_string_init(insertQuery.str()), bindings.length()/2));
        graphProgressMappings[0].mapper.fromXML(update, 0, inXML, graphProgressMappings[0].xpath);
        graphProgressMappings[1].mapper.fromXML(update, 1, running, graphProgressMappings[1].xpath);
        graphProgressMappings[2].mapper.fromXML(update, 2, running, graphProgressMappings[2].xpath);
        check(cass_statement_bind_int64(update, 3, 0)); // subgraphId can't be null, as it's in the key
        check(cass_batch_add_statement(batch, update));
    }
    CassandraFuture futureBatch(cass_session_execute_batch(session, batch));
    futureBatch.wait("execute");
}

extern void cassandraToGraphProgressXML(CassSession *session, const char *wuid)
{
    CassandraResult result(fetchDataForWu(wuid, session, graphProgressMappings));
    Owned<IPTree> progress = createPTree(wuid);
    CassandraIterator rows(cass_iterator_from_result(result));
    while (cass_iterator_next(rows))
    {
        CassandraIterator cols(cass_iterator_from_row(cass_iterator_get_row(rows)));
        unsigned colidx = 1;  // wuid is not returned
        IPTree *ptree = progress;
        while (cass_iterator_next(cols))
        {
            assertex(graphProgressMappings[colidx].columnName);
            const CassValue *value = cass_iterator_get_column(cols);
            // NOTE - this relies on the fact that progress is NULL when subgraphId=0, so that the status and id fields
            // get set on the graph instead of on the child node in those cases.
            if (value && !cass_value_is_null(value))
                ptree = graphProgressMappings[colidx].mapper.toXML(ptree, graphProgressMappings[colidx].xpath, value);
            colidx++;
        }
    }
    StringBuffer out;
    toXML(progress, out, 0, XML_SortTags|XML_Format);
    printf("%s", out.str());
}
*/

extern IPTree *cassandraToWorkunitXML(CassSession *session, const char *wuid)
{
    CassandraResult result(fetchDataForKey(wuid, session, workunitsMappings));
    CassandraIterator rows(cass_iterator_from_result(result));
    if (cass_iterator_next(rows)) // should just be one
    {
        Owned<IPTree> wuXML = createPTree(wuid);
        wuXML->setProp("@xmlns:xsi", "http://www.w3.org/1999/XMLSchema-instance");
        CassandraIterator cols(cass_iterator_from_row(cass_iterator_get_row(rows)));
        unsigned colidx = 1;  // wuid is not returned
        while (cass_iterator_next(cols))
        {
            assertex(workunitsMappings[colidx].columnName);
            const CassValue *value = cass_iterator_get_column(cols);
            if (value && !cass_value_is_null(value))
                workunitsMappings[colidx].mapper.toXML(wuXML, workunitsMappings[colidx].xpath, value);
            colidx++;
        }
        return wuXML.getClear();
    }
    else
        return NULL;
}

static const CassValue *getSingleResult(const CassResult *result)
{
    const CassRow *row = cass_result_first_row(result);
    if (row)
        return cass_row_get_column(row, 0);
    else
        return NULL;
}

static StringBuffer &getCassString(StringBuffer &str, const CassValue *value)
{
    CassString output;
    check(cass_value_get_string(value, &output));
    return str.append(output.length, output.data);
}

/*
extern void cassandraTestGraphProgressXML()
{
    CassandraCluster cluster(cass_cluster_new());
    cass_cluster_set_contact_points(cluster, "127.0.0.1");
    CassandraSession session(cass_session_new());
    CassandraFuture future(cass_session_connect_keyspace(session, cluster, "hpcc"));
    future.wait("connect");

    ensureTable(session, graphProgressMappings);
    Owned<IPTree> inXML = createPTreeFromXMLFile("/data/rchapman/hpcc/testing/regress/ecl/a.xml");
    graphProgressXMLtoCassandra(session, inXML);
    const char *wuid = inXML->queryName();
    cassandraToGraphProgressXML(session, wuid);
}

extern void cassandraTest()
{
    cassandraTestWorkunitXML();
    //cassandraTestGraphProgressXML();
}
*/

class CCassandraWorkUnit : public CLocalWorkUnit
{
public:
    IMPLEMENT_IINTERFACE;
    CCassandraWorkUnit(ICassandraSession *_sessionCache, IPTree *wuXML, ISecManager *secmgr, ISecUser *secuser)
        : sessionCache(_sessionCache), CLocalWorkUnit(secmgr, secuser)
    {
        CLocalWorkUnit::loadPTree(wuXML);
        allDirty = false;   // Debatable... depends where the XML came from! If we read it from Cassandra. it's not. Otherwise, it is...
        memset(childLoaded, 0, sizeof(childLoaded));
        abortDirty = true;
        abortState = false;
    }
    ~CCassandraWorkUnit()
    {
    }

    virtual void forceReload()
    {
        printStackReport();
        UNIMPLEMENTED;
        abortDirty = true;
    }

    virtual void cleanupAndDelete(bool deldll, bool deleteOwned, const StringArray *deleteExclusions)
    {
        const char *wuid = queryWuid();
        CLocalWorkUnit::cleanupAndDelete(deldll, deleteOwned, deleteExclusions);
        if (!batch)
            batch.setown(new CassandraBatch(cass_batch_new(CASS_BATCH_TYPE_UNLOGGED)));
        deleteChildren(wuid);
        deleteSecondaries(wuid);
        Owned<CassandraPrepared> prepared = sessionCache->prepareStatement("DELETE from workunits where wuid=?;");
        CassandraStatement update(cass_prepared_bind(*prepared));
        check(cass_statement_bind_string(update, 0, cass_string_init(wuid)));
        check(cass_batch_add_statement(*batch, update));
        CassandraFuture futureBatch(cass_session_execute_batch(sessionCache->querySession(), *batch));
        futureBatch.wait("execute");
        batch.clear();
    }

    virtual void commit()
    {
        CLocalWorkUnit::commit();
        if (sessionCache->queryTraceLevel() >= 8)
        {
            StringBuffer s; toXML(p, s); DBGLOG("CCassandraWorkUnit::commit\n%s", s.str());
        }
        if (batch)
        {
            const char *wuid = queryWuid();
            if (prev) // Holds the values of the "basic" info at the last commit
                updateSecondaries(wuid);
            simpleXMLtoCassandra(sessionCache, *batch, workunitsMappings, p);  // This just does the parent row
            if (allDirty)
            {
                // MORE - this delete is technically correct, but if we assert that the only place that copyWorkUnit is used is to populate an
                // empty newly-created WU, it is unnecessary.
                //deleteChildren(wuid);

                wuResultsXMLtoCassandra(sessionCache, *batch, p, "Results/Result");
                wuVariablesXMLtoCassandra(sessionCache, *batch, p, "Variables/Variable", ResultSequenceStored);
                wuVariablesXMLtoCassandra(sessionCache, *batch, p, "Temporaries/Variable", ResultSequenceInternal); // NOTE - lookups may also request ResultSequenceOnce
                childXMLtoCassandra(sessionCache, *batch, wuExceptionsMappings, p, "Exceptions/Exception", 0);
                childXMLtoCassandra(sessionCache, *batch, wuStatisticsMappings, p, "Statistics/Statistic", 0);
            }
            else
            {
                ResultPTreeIterator dirtyResultsIterator(dirtyResults);
                childXMLtoCassandra(sessionCache, *batch, wuResultsMappings, wuid, &dirtyResultsIterator, 0);  // MORE - all the other dirty subtrees... TBD
            }
            CassandraFuture futureBatch(cass_session_execute_batch(sessionCache->querySession(), *batch));
            futureBatch.wait("execute");
            batch.setown(new CassandraBatch(cass_batch_new(CASS_BATCH_TYPE_UNLOGGED))); // Commit leaves it locked...
            prev.clear();
            allDirty = false;
        }
        else
            DBGLOG("No batch present??");
    }

    virtual void setUser(const char *user)
    {
        if (trackSecondaryChange(user, ownerMappings))
            CLocalWorkUnit::setUser(user);
    }
    virtual void setClusterName(const char *cluster)
    {
        if (trackSecondaryChange(cluster, clusterMappings))
            CLocalWorkUnit::setClusterName(cluster);
    }
    virtual void setJobName(const char *jobname)
    {
        if (trackSecondaryChange(jobname, jobnameMappings))
            CLocalWorkUnit::setJobName(jobname);
    }

    virtual void _lockRemote()
    {
        // Ignore locking for now!
//        printStackReport();
//        UNIMPLEMENTED;
        batch.setown(new CassandraBatch(cass_batch_new(CASS_BATCH_TYPE_UNLOGGED)));
    }

    virtual void _unlockRemote()
    {
//        printStackReport();
//        UNIMPLEMENTED;
        commit();
        batch.clear();
    }

    virtual void subscribe(WUSubscribeOptions options)
    {
//        printStackReport();
//        UNIMPLEMENTED;
    }

    virtual void unsubscribe()
    {
//        printStackReport();
//        UNIMPLEMENTED;
    }

    virtual bool aborting() const
    {
        return false;
        // MORE - work out what to do about aborts in Cassandra
//        printStackReport();
//        UNIMPLEMENTED;
    }

    virtual IWUResult * updateResultByName(const char * name)
    {
        return noteDirty(CLocalWorkUnit::updateResultByName(name));
    }
    virtual IWUResult * updateResultBySequence(unsigned seq)
    {
        return noteDirty(CLocalWorkUnit::updateResultBySequence(seq));
    }
    virtual IWUResult * updateTemporaryByName(const char * name)
    {
        return noteDirty(CLocalWorkUnit::updateTemporaryByName(name));
    }
    virtual IWUResult * updateVariableByName(const char * name)
    {
        return noteDirty(CLocalWorkUnit::updateVariableByName(name));
    }

    virtual void copyWorkUnit(IConstWorkUnit *cached, bool all)
    {
        // Make sure that any required updates to the secondary files happen
        IPropertyTree *fromP = queryExtendedWU(cached)->queryPTree();
        for (const CassandraXmlMapping **mapping = secondaryTables; *mapping; mapping++)
            trackSecondaryChange(fromP->queryProp(mapping[0]->xpath), *mapping);
        for (const ChildTableInfo **table = childTables; *table != NULL; table++)
            checkChildLoaded(**table);
        CLocalWorkUnit::copyWorkUnit(cached, all);
        memset(childLoaded, 1, sizeof(childLoaded));
        allDirty = true;
    }

    virtual void _loadResults() const
    {
        checkChildLoaded(wuResultsTable);        // Lazy populate the Results branch of p from Cassandra
        CLocalWorkUnit::_loadResults();
    }

    virtual void _loadStatistics() const
    {
        checkChildLoaded(wuStatisticsTable);        // Lazy populate the Statistics branch of p from Cassandra
        CLocalWorkUnit::_loadStatistics();
    }

    virtual IPropertyTree *queryPTree() const
    {
        // If anyone wants the whole ptree, we'd better make sure we have fully loaded it...
        CriticalBlock b(crit);
        for (const ChildTableInfo **table = childTables; *table != NULL; table++)
            checkChildLoaded(**table);
        return p;
    }
protected:
    // Delete child table rows
    void deleteChildren(const char *wuid)
    {
        for (const ChildTableInfo **table = childTables; *table != NULL; table++)
            deleteChildByWuid(table[0]->mappings, wuid, sessionCache, *batch);
    }

    // Lazy-populate a portion of WU xml from a child table
    void checkChildLoaded(const ChildTableInfo &child) const
    {
        // NOTE - should be called inside critsec
        if (!childLoaded[child.index])
        {
            cassandraToWuChildXML(sessionCache->querySession(), child.mappings, child.parentElement, child.childElement, queryWuid(), p);
            childLoaded[child.index] = true;
        }
    }

    // Update secondary tables (used to search wuids by orner, state, jobname etc)

    void updateSecondaryTable(const CassandraXmlMapping *mappings, const char *wuid, const char *prevKey)
    {
        deleteSecondaryByKey(mappings, wuid, prevKey, sessionCache, *batch);
        if (p->hasProp(mappings[0].xpath))
            simpleXMLtoCassandra(sessionCache, *batch, mappings, p);
    }

    void deleteSecondaries(const char *wuid)
    {
        for (const CassandraXmlMapping **mapping = secondaryTables; *mapping; mapping++)
        {
            deleteSecondaryByKey(*mapping, wuid, p->queryProp(mapping[0]->xpath), sessionCache, *batch);
        }
    }

    void updateSecondaries(const char *wuid)
    {
        for (const CassandraXmlMapping **mapping = secondaryTables; *mapping; mapping++)
        {
            updateSecondaryTable(*mapping, wuid, prev->queryProp(mapping[0]->xpath));
        }
    }

    // Keep track of previously committed values for fields that we have a secondary table for, so that we can update them appropriately when we commit

    bool trackSecondaryChange(const char *newval, const CassandraXmlMapping *mappings)
    {
        if (!newval)
            newval = "";
        const char *oldval = p->queryProp(mappings->xpath);
        if (!oldval)
             oldval = "";
         if (streq(newval, oldval))
            return false;  // No change
        if (!prev)
        {
            prev.setown(createPTree());
            prev->setProp(mappings->xpath, oldval);
        }
        else if (!prev->hasProp(mappings->xpath))
            prev->setProp(mappings->xpath, oldval);
        return true;
    }

    // Allows us to iterate over an array of IPTrees - MORE this could be in jptree? Should save the trees not the results I suspect.

    class ResultPTreeIterator : implements CInterfaceOf<IPTreeIterator>
    {
    public:
        ResultPTreeIterator(IArrayOf<IWUResult> &_results) : r(_results), idx(0), p(NULL) {}
        virtual bool first() { idx = 0; return isValid(); }
        virtual bool next() { idx++; return isValid(); }
        virtual bool isValid()
        {
            if (r.isItem(idx))
            {
                p = r.item(idx).queryPTree();
                return true;
            }
            else
            {
                p = NULL;
                return false;
            }
        }
        virtual IPropertyTree & query() { return *p; }
    protected:
        IArrayOf<IWUResult> &r;
        IPropertyTree *p;
        unsigned idx;
    };
    IWUResult *noteDirty(IWUResult *result)
    {
        if (result)
            dirtyResults.append(*LINK(result));
        return result;
    }
    const ICassandraSession *sessionCache;
    mutable bool abortDirty;
    mutable bool abortState;
    mutable bool childLoaded[ChildTablesSize];
    bool allDirty;
    Owned<IPTree> prev;

    Owned<CassandraBatch> batch;
    IArrayOf<IWUResult> dirtyResults;
};

class CCasssandraWorkUnitFactory : public CWorkUnitFactory, implements ICassandraSession
{
public:
    CCasssandraWorkUnitFactory(const IPropertyTree *props) : cluster(cass_cluster_new()), randomizeSuffix(0)
    {
        StringArray options;
        Owned<IPTreeIterator> it = props->getElements("Option");
        ForEach(*it)
        {
            IPTree &item = it->query();
            const char *opt = item.queryProp("@name");
            const char *val = item.queryProp("@value");
            if (opt && val)
            {
                if (strieq(opt, "randomWuidSuffix"))
                    randomizeSuffix = atoi(val);
                else if (strieq(opt, "traceLevel"))
                    traceLevel = atoi(val);
                else
                {
                    VStringBuffer optstr("%s=%s", opt, val);
                    options.append(optstr);
                }
            }
        }
        cluster.setOptions(options);
        if (cluster.keyspace.isEmpty())
            cluster.keyspace.set("hpcc");
        connect();
    }

    ~CCasssandraWorkUnitFactory()
    {
    }

    virtual CLocalWorkUnit* _createWorkUnit(const char *wuid, ISecManager *secmgr, ISecUser *secuser)
    {
        unsigned suffix;
        unsigned suffixLength;
        if (randomizeSuffix)  // May need to enable this option if you are expecting to create hundreds of workunits / second
        {
            suffix = rand();
            suffixLength = randomizeSuffix;
        }
        else
        {
            suffix = 0;
            suffixLength = 0;
        }
        Owned<CassandraPrepared> prepared = prepareStatement("INSERT INTO workunits (wuid) VALUES (?) IF NOT EXISTS;");
        loop
        {
            // Create a unique WUID by adding suffixes until we managed to add a new value
            StringBuffer useWuid(wuid);
            if (suffix)
            {
                useWuid.append("-");
                for (unsigned i = 0; i < suffixLength; i++)
                {
                    useWuid.appendf("%c", '0'+suffix%10);
                    suffix /= 10;
                }
            }
            CassandraStatement statement(cass_prepared_bind(*prepared));
            check(cass_statement_bind_string(statement, 0, cass_string_init(useWuid.str())));
            if (traceLevel >= 2)
                DBGLOG("Try creating %s", useWuid.str());
            CassandraFuture future(cass_session_execute(session, statement));
            future.wait("execute");
            CassandraResult result(cass_future_get_result(future));
            CassString columnName = cass_result_column_name(result, 0);
            if (cass_result_column_count(result)==1)
            {
                // A single column result indicates success, - the single column should be called '[applied]' and have the value 'true'
                // If there are multiple columns it will be '[applied]' (value false) and the fields of the existing row
                Owned<IPTree> wuXML = createPTree(useWuid);
                wuXML->setProp("@xmlns:xsi", "http://www.w3.org/1999/XMLSchema-instance");
                Owned<CLocalWorkUnit> wu = new CCassandraWorkUnit(this, wuXML.getClear(), secmgr, secuser);
                wu->lockRemote(true);
                return wu.getClear();
            }
            suffix = rand();
            if (suffixLength<9)
                suffixLength++;
        }
    }
    virtual CLocalWorkUnit* _openWorkUnit(const char *wuid, bool lock, ISecManager *secmgr, ISecUser *secuser)
    {
        // MORE - what to do about lock?
        Owned<IPTree> wuXML = cassandraToWorkunitXML(session, wuid);
        if (wuXML)
            return new CCassandraWorkUnit(this, wuXML.getClear(), secmgr, secuser);
        else
            return NULL;
    }
    virtual CLocalWorkUnit* _updateWorkUnit(const char *wuid, ISecManager *secmgr, ISecUser *secuser)
    {
        // Ignore locking for now
        // Note - in Dali, this would lock for write, whereas _openWorkUnit would either lock for read (if lock set) or not lock at all
        Owned<IPTree> wuXML = cassandraToWorkunitXML(session, wuid);
        Owned<CLocalWorkUnit> wu = new CCassandraWorkUnit(this, wuXML.getClear(), secmgr, secuser);
        wu->lockRemote(true);
        return wu.getClear();
    }

    virtual IWorkUnit * getGlobalWorkUnit(ISecManager *secmgr = NULL, ISecUser *secuser = NULL) { UNIMPLEMENTED; }
    virtual IConstWorkUnitIterator * getWorkUnitsByOwner(const char * owner, ISecManager *secmgr, ISecUser *secuser)
    {
        return getWorkUnitsByXXX(ownerMappings, owner, secmgr, secuser);
    }
    virtual IConstWorkUnitIterator * getWorkUnitsByState(WUState state, ISecManager *secmgr, ISecUser *secuser) { UNIMPLEMENTED; }
    virtual IConstWorkUnitIterator * getWorkUnitsByECL(const char * ecl, ISecManager *secmgr, ISecUser *secuser) { UNIMPLEMENTED; }
    virtual IConstWorkUnitIterator * getWorkUnitsByCluster(const char * cluster, ISecManager *secmgr, ISecUser *secuser)
    {
        return getWorkUnitsByXXX(clusterMappings, cluster, secmgr, secuser);
    }
    virtual IConstWorkUnitIterator * getWorkUnitsByXPath(const char * xpath, ISecManager *secmgr, ISecUser *secuser) { UNIMPLEMENTED; }
    virtual IConstWorkUnitIterator * getWorkUnitsSorted(WUSortField * sortorder, WUSortField * filters, const void * filterbuf,
                                                        unsigned startoffset, unsigned maxnum, const char * queryowner, __int64 * cachehint, unsigned *total,
                                                        ISecManager *secmgr, ISecUser *secuser) { UNIMPLEMENTED; }
    virtual unsigned numWorkUnits()
    {
        Owned<CassandraPrepared> prepared = prepareStatement("SELECT COUNT(*) FROM workunits;");
        CassandraStatement statement(cass_prepared_bind(*prepared));
        CassandraFuture future(cass_session_execute(session, statement));
        future.wait("select count(*)");
        CassandraResult result(cass_future_get_result(future));
        return getUnsignedResult(NULL, getSingleResult(result));
    }
    /*
    virtual void descheduleAllWorkUnits(ISecManager *secmgr, ISecUser *secuser) { UNIMPLEMENTED; }
    virtual IConstQuerySetQueryIterator * getQuerySetQueriesSorted(WUQuerySortField *sortorder, WUQuerySortField *filters, const void *filterbuf, unsigned startoffset, unsigned maxnum, __int64 *cachehint, unsigned *total, const MapStringTo<bool> *subset) { UNIMPLEMENTED; }
    virtual bool isAborting(const char *wuid) const { UNIMPLEMENTED; }
    virtual void clearAborting(const char *wuid) { UNIMPLEMENTED; }
    */
    virtual WUState waitForWorkUnit(const char * wuid, unsigned timeout, bool compiled, bool returnOnWaitState)
    {
        VStringBuffer select("select state from workunits where wuid = '%s';", wuid);
        CassandraStatement statement(cass_statement_new(cass_string_init(select.str()), 0));
        unsigned start = msTick();
        loop
        {
            CassandraFuture future(cass_session_execute(session, statement));
            future.wait("Lookup wu state");
            CassandraResult result(cass_future_get_result(future));
            const CassValue *value = getSingleResult(result);
            if (value == NULL)
                return WUStateUnknown;
            CassString output;
            check(cass_value_get_string(value, &output));
            StringBuffer stateStr(output.length, output.data);
            WUState state = getWorkUnitState(stateStr);
            switch (state)
            {
            case WUStateCompiled:
            case WUStateUploadingFiles:
                if (compiled)
                    return state;
                break;
            case WUStateCompleted:
            case WUStateFailed:
            case WUStateAborted:
                return state;
            case WUStateWait:
                if (returnOnWaitState)
                    return state;
                break;
            case WUStateCompiling:
            case WUStateRunning:
            case WUStateDebugPaused:
            case WUStateDebugRunning:
            case WUStateBlocked:
            case WUStateAborting:
                // MORE - can see if agent still running, and set to failed if it is not
                break;
            }
            unsigned waited = msTick() - start;
            if (timeout != -1 && waited > timeout)
            {
                return WUStateUnknown;
                break;
            }
            Sleep(1000); // MORE - may want to back off as waited gets longer...
        }
    }

    unsigned validateRepository(bool fix)
    {
        unsigned errCount = 0;
        // MORE - if the batch gets too big you may need to flush it occasionally
        CassandraBatch batch(fix ? cass_batch_new(CASS_BATCH_TYPE_LOGGED) : NULL);
        // 1. Check that every entry in main wu table has matching entries in secondary tables
        CassandraResult result(fetchDataForKey(NULL, session, workunitInfoMappings));
        CassandraIterator rows(cass_iterator_from_result(result));
        while (cass_iterator_next(rows))
        {
            Owned<IPTree> wuXML = rowToPTree(NULL, workunitInfoMappings, cass_iterator_get_row(rows));
            const char *wuid = wuXML->queryName();
            // For each secondary file, check that we get matching XML
            for (const CassandraXmlMapping **mapping = secondaryTables; *mapping; mapping++)
                errCount += validateSecondary(*mapping, wuid, wuXML, batch);
        }
        // 2. Check that there are no orphaned entries in secondary or child tables
        for (const CassandraXmlMapping **mapping = secondaryTables; *mapping; mapping++)
            errCount += checkOrphans(*mapping, 1, batch);
        for (const ChildTableInfo **table = childTables; *table != NULL; table++)
            errCount += checkOrphans(table[0]->mappings, 0, batch);
        // 3. Commit fixes
        if (batch)
        {
            CassandraFuture futureBatch(cass_session_execute_batch(session, batch));
            futureBatch.wait("Fix_repository");
        }
        return errCount;
    }

    virtual void deleteRepository(bool recreate)
    {
        // USE WITH CARE!
        session.set(cass_session_new());
        CassandraFuture future(cass_session_connect(session, cluster));
        future.wait("connect without keyspace to delete");
        VStringBuffer deleteKeyspace("DROP KEYSPACE IF EXISTS %s;", cluster.keyspace.get());
        executeSimpleCommand(session, deleteKeyspace);
        if (recreate)
            connect();
        else
            session.set(NULL);
    }

    virtual void createRepository()
    {
        session.set(cass_session_new());
        CassandraFuture future(cass_session_connect(session, cluster));
        future.wait("connect without keyspace");
        VStringBuffer create("CREATE KEYSPACE IF NOT EXISTS %s WITH replication = { 'class': 'SimpleStrategy', 'replication_factor': '1' } ;", cluster.keyspace.get()); // MORE - options from props? Not 100% sure if they are appropriate.
        executeSimpleCommand(session, create);
        connect();
        ensureTable(session, workunitsMappings);
        for (const CassandraXmlMapping **mapping = secondaryTables; *mapping; mapping++)
            ensureTable(session, *mapping);
        for (const ChildTableInfo **table = childTables; *table != NULL; table++)
            ensureTable(session, table[0]->mappings);
    }

    // Interface ICassandraSession
    virtual CassSession *querySession() const { return session; };
    virtual unsigned queryTraceLevel() const { return traceLevel; };
    virtual CassandraPrepared *prepareStatement(const char *query) const
    {
        assertex(session);
        CriticalBlock b(cacheCrit);
        Linked<CassandraPrepared> cached = preparedCache.getValue(query);
        if (cached)
        {
            if (traceLevel >= 2)
                DBGLOG("prepareStatement: Reusing %s", query);
            return cached.getClear();
        }
        {
            if (traceLevel >= 2)
                DBGLOG("prepareStatement: Binding %s", query);
            // We don't want to block cache lookups while we prepare a new bound statement
            // Note - if multiple threads try to prepare the same (new) statement at the same time, it's not catastrophic
            CriticalUnblock b(cacheCrit);
            CassandraFuture futurePrep(cass_session_prepare(session, cass_string_init(query)));
            futurePrep.wait("prepare statement");
            cached.setown(new CassandraPrepared(cass_future_get_prepared(futurePrep)));
        }
        preparedCache.setValue(query, cached); // NOTE - this links parameter
        return cached.getClear();
    }
private:
    void connect()
    {
        session.set(cass_session_new());
        CassandraFuture future(cass_session_connect_keyspace(session, cluster, cluster.keyspace));
        future.wait("connect with keyspace");
    }
    bool checkWuExists(const char *wuid)
    {
        Owned<CassandraPrepared> prepared = prepareStatement("SELECT COUNT(*) FROM workunits where wuid=?;");
        CassandraStatement statement(cass_prepared_bind(*prepared));
        cass_statement_bind_string(statement, 0, cass_string_init(wuid));
        CassandraFuture future(cass_session_execute(session, statement));
        future.wait("select count(*)");
        CassandraResult result(cass_future_get_result(future));
        return getUnsignedResult(NULL, getSingleResult(result)) != 0; // Shouldn't be more than 1, either
    }

    IConstWorkUnitIterator * getWorkUnitsByXXX(const CassandraXmlMapping *mappings, const char *key, ISecManager *secmgr, ISecUser *secuser)
    {
        if (!key || !*key)
            mappings=workunitInfoMappings;   // Historically, providing no value on a call to getWorkUnitsByOwner (for example) filter meant unfiltered...
        CassandraResult result(fetchDataForKey(key, session, mappings));
        Owned<IPTree> parent = createPTree("WorkUnits");
        CassandraIterator rows(cass_iterator_from_result(result));
        while (cass_iterator_next(rows))
        {
            Owned<IPTree> wuXML = rowToPTree(key, mappings, cass_iterator_get_row(rows));
            const char *wuid = wuXML->queryName();
            parent->addPropTree(wuid, wuXML.getClear());
        }
        Owned<IPropertyTreeIterator> iter = parent->getElements("*");
        return createConstWUIterator(iter, secmgr, secuser);
    }

    unsigned validateSecondary(const CassandraXmlMapping *mappings, const char *wuid, IPTree *wuXML, CassBatch *batch)
    {
        unsigned errCount = 0;
        const char *childKey = wuXML->queryProp(mappings->xpath);
        if (childKey && *childKey)
        {
            CassandraResult result(fetchDataForKeyAndWuid(childKey, wuid, session, mappings));
            switch (cass_result_row_count(result))
            {
            case 0:
                DBGLOG("Missing secondary data in %s for wuid=%s %s=%s", queryTableName(mappings), wuid, mappings->columnName, childKey);
                if (batch)
                    simpleXMLtoCassandra(this, batch, mappings, wuXML);
                errCount++;
                break;
            case 1:
            {
                Owned<IPTree> secXML = rowToPTree(NULL, mappings+2, cass_result_first_row(result));   // wuid and key not returned
                secXML->setProp(mappings->xpath, childKey);
                secXML->renameProp("/", wuid);
                if (!areMatchingPTrees(wuXML, secXML))
                {
                    DBGLOG("Mismatched data in %s for wuid %s", queryTableName(mappings), wuid);
                    if (batch)
                        simpleXMLtoCassandra(this, batch, mappings, wuXML);
                    errCount++;
                }
                break;
            }
            default:
                DBGLOG("Multiple secondary data %d in %s for wuid %s", (int) cass_result_row_count(result), queryTableName(mappings), wuid); // This should be impossible!
                if (batch)
                {
                    deleteSecondaryByKey(mappings, wuid, childKey, this, batch);
                    simpleXMLtoCassandra(this, batch, mappings, wuXML);
                }
                break;
            }
        }
        return errCount;
    }

    unsigned checkOrphans(const CassandraXmlMapping *mappings, unsigned wuidIndex, CassBatch *batch)
    {
        unsigned errCount = 0;
        CassandraResult result(fetchDataForKey(NULL, session, mappings));
        CassandraIterator rows(cass_iterator_from_result(result));
        while (cass_iterator_next(rows))
        {
            const CassRow *row = cass_iterator_get_row(rows);
            StringBuffer wuid;
            getCassString(wuid, cass_row_get_column(row, wuidIndex));
            if (!checkWuExists(wuid))
            {
                DBGLOG("Orphaned data in %s for wuid=%s", queryTableName(mappings), wuid.str());
                if (batch)
                {
                    if (wuidIndex)
                    {
                        StringBuffer key;
                        getCassString(key, cass_row_get_column(row, 0));
                        deleteSecondaryByKey(mappings, wuid, key, this, batch);
                    }
                    else
                        deleteChildByWuid(mappings, wuid, this, batch);
                }
                errCount++;
            }
        }
        return errCount;
    }


    IPTree *rowToPTree(const char *key, const CassandraXmlMapping *mappings, const CassRow *row)
    {
        CassandraIterator cols(cass_iterator_from_row(row));
        Owned<IPTree> xml = createPTree("row");  // May be overwritten below if wuid field is processed
        if (key && *key)
        {
            xml->setProp(mappings->xpath, key);
            mappings++;
        }
        while (cass_iterator_next(cols))
        {
            assertex(mappings->columnName);
            const CassValue *value = cass_iterator_get_column(cols);
            if (value && !cass_value_is_null(value))
                mappings->mapper.toXML(xml, mappings->xpath, value);
            mappings++;
        }
        return xml.getClear();

    }

    unsigned randomizeSuffix;
    unsigned traceLevel;
    CassandraCluster cluster;
    CassandraSession session;
    mutable CriticalSection cacheCrit;
    mutable MapStringToMyClass<CassandraPrepared> preparedCache;
};


} // namespace

extern "C" EXPORT IWorkUnitFactory *createWorkUnitFactory(const IPropertyTree *props)
{
    return new cassandraembed::CCasssandraWorkUnitFactory(props);
}

