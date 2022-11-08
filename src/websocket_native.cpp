#include "websocket_connection_base.h"
#include "websocket_connection_ssl.h"
#include "websocket_connection.h"
#include "url.hpp"

enum
{
    WebSocket_JSON,
    Websocket_STRING,
};

HandleError websocket_read_handle(Handle_t hndl, IPluginContext *p_context, websocket_connection_base **obj)
{
    HandleSecurity sec;

    sec.pOwner = p_context->GetIdentity();
    sec.pIdentity = myself->GetIdentity();
    HandleError herr;
    if ((herr = handlesys->ReadHandle(hndl, websocket_handle_type, &sec, reinterpret_cast<void **>(obj))) != HandleError_None)
    {
        p_context->ReportError("Invalid WebSocket handle (error %d)", herr);
        return herr;
    }

    return HandleError_None;
}

static json_t *GetJSONFromHandle(IPluginContext *pContext, Handle_t hndl)
{
    HandleError err;
    HandleSecurity sec(pContext->GetIdentity(), myself->GetIdentity());

    json_t *json;
    if ((err = handlesys->ReadHandle(hndl, htJSON, &sec, (void **)&json)) != HandleError_None)
    {
        pContext->ThrowNativeError("Invalid JSON handle %x (error %d)", hndl, err);
        return nullptr;
    }

    return json;
}

static cell_t native_Connect(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    if (websocket_read_handle(params[1], p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    connection->connect();
    return 0;
}

static cell_t native_Close(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    if (websocket_read_handle(params[1], p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    connection->close();
    return 0;
}

static cell_t native_SetReadCallback(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    Handle_t hndl_websocket = params[1];
    if (websocket_read_handle(hndl_websocket, p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    uint32_t callback_type = params[2];
    IPluginFunction *callback = p_context->GetFunctionById((funcid_t)params[3]);
    if (!callback)
    {
        p_context->ReportError("Invalid handler callback provided");
        return 0;
    }

    cell_t data = params[4];
    switch (callback_type)
    {
    case WebSocket_JSON:
        connection->set_read_callback([callback, hndl_websocket, p_context, data](auto buffer, auto size)
                                      {
        std::string message(reinterpret_cast<const char*>(buffer), size);
        free(buffer);

            g_RipExt.Defer([callback, hndl_websocket, message, p_context, data]() {
			    json_t *object = json_loads(message.data(), 0, nullptr);
			    Handle_t handle = handlesys->CreateHandle(htJSON, object, p_context->GetIdentity(), myself->GetIdentity(), nullptr);
			    callback->PushCell(hndl_websocket);
			    callback->PushCell(handle);
			    callback->PushCell(data);
			    callback->Execute(nullptr);
            }); });
        break;
    case Websocket_STRING:
        connection->set_read_callback([callback, hndl_websocket, p_context, data](auto buffer, auto size)
                                      {
        std::string message(reinterpret_cast<const char*>(buffer), size);
        free(buffer);

            g_RipExt.Defer([callback, hndl_websocket, message, p_context, data]() {
			    callback->PushCell(hndl_websocket);
			    callback->PushString(message.data());
			    callback->PushCell(data);
			    callback->Execute(nullptr);
            }); });
        break;
    }
    return 0;
}

static cell_t native_SetDisconnectCallback(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    Handle_t hndl_websocket = params[1];
    if (websocket_read_handle(hndl_websocket, p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    IPluginFunction *callback = p_context->GetFunctionById((funcid_t)params[2]);
    if (!callback)
    {
        p_context->ReportError("Invalid handler callback provided");
        return 0;
    }

    cell_t data = params[3];

    connection->set_disconnect_callback([callback, hndl_websocket, p_context, data]()
                                        { g_RipExt.Defer([callback, hndl_websocket, p_context, data]()
                                                         {
            callback->PushCell(hndl_websocket);
            callback->PushCell(data);
            callback->Execute(nullptr); }); });

    return 0;
}

static cell_t native_SetConnectCallback(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    Handle_t hndl_websocket = params[1];
    if (websocket_read_handle(hndl_websocket, p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    auto callback = p_context->GetFunctionById((funcid_t)params[2]);
    if (!callback)
    {
        p_context->ReportError("Invalid handler callback provided");
        return 0;
    }

    cell_t data = params[3];

    connection->set_connect_callback([callback, hndl_websocket, p_context, data]()
                                     { g_RipExt.Defer([callback, hndl_websocket, p_context, data]()
                                                      {
            callback->PushCell(hndl_websocket);
            callback->PushCell(data);
            callback->Execute(nullptr); }); });

    return 0;
}

static cell_t native_Write(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    if (websocket_read_handle(params[1], p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    json_t *object = GetJSONFromHandle(p_context, params[2]);

    if (object == nullptr)
    {
        return 0;
    }

    char *result;

    result = json_dumps(object, 0);

    connection->write(boost::asio::buffer(result, strlen(result)));
    return 0;
}

static cell_t native_WriteString(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    if (websocket_read_handle(params[1], p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    char *result;

    p_context->LocalToString(params[2], &result);

    connection->write(boost::asio::buffer(result, strlen(result)));
    return 0;
}

static cell_t native_SetHeader(IPluginContext *p_context, const cell_t *params)
{
    websocket_connection_base *connection;
    if (websocket_read_handle(params[1], p_context, &connection) != HandleError_None)
    {
        return 0;
    }

    char *header, *value;
    p_context->LocalToString(params[2], &header);
    p_context->LocalToString(params[3], &value);
    connection->set_header(std::string(header), std::string(value));
    return 0;
}

static cell_t native_WebSocket(IPluginContext *p_context, const cell_t *params)
{
    char *s_url;
    p_context->LocalToString(params[1], &s_url);
    try
    {
        Url url(s_url);
        websocket_connection_base *connection;
        if (url.path().empty())
        {
            url.path("/");
        }

        std::string path(url.path());

        for (unsigned int i = 0; i < url.query().size(); i++)
        {
            if (i == 0)
            {
                path.append("?");
            }
            else
            {
                path.append("&");
            }
            Url::KeyVal q = url.query(i);
            path.append(q.key());
            path.append("=");
            path.append(q.val());
        }

        std::string host(url.host());
        if (url.scheme() == "wss")
        {
            if (url.port().empty())
            {
                url.port("443");
            }
            connection = new websocket_connection_ssl(host, path, stoi(url.port()));
        }
        else if (url.scheme() == "ws")
        {
            if (url.port().empty())
            {
                url.port("80");
            }
            connection = new websocket_connection(host, path, stoi(url.port()));
        }

        return handlesys->CreateHandle(websocket_handle_type, connection, p_context->GetIdentity(), myself->GetIdentity(), nullptr);
    }
    catch (...)
    {
        p_context->ReportError("Invalid websocket URL: %s", s_url);
        return 0;
    }
}

const sp_nativeinfo_t sm_websocket_natives[] = {
    {"WebSocket.WebSocket",             native_WebSocket},
    {"WebSocket.Connect",               native_Connect},
    {"WebSocket.SetHeader",             native_SetHeader},
    {"WebSocket.Close",                 native_Close},
    {"WebSocket.SetReadCallback",       native_SetReadCallback},
    {"WebSocket.SetDisconnectCallback", native_SetDisconnectCallback},
    {"WebSocket.SetConnectCallback",    native_SetConnectCallback},
    {"WebSocket.Write",                 native_Write},
    {"WebSocket.WriteString",           native_WriteString},
    {nullptr,                           nullptr}
};