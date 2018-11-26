// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <ChakraCore.h>
#include <ChakraDebugProtocolHandler.h>

typedef struct JsDebugService__* JsDebugService;

/// <summary>Creates a <seealso cref="JsDebugProtocolHandler" /> instance.</summary>
/// <param name="service">The newly created instance.</param>
/// <param name="title">Instance title, for display in a list of available services (e.g., Chrome's Remote Target list).</param>
/// <param name="description">Instance description, for display available service list.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugServiceCreate(_Out_ JsDebugService* service, const char* title = nullptr, const char* description = nullptr,
    const BYTE* favIcon = nullptr, size_t favIconSize = 0);

/// <summary>Destroys the instance object.</summary>
/// <param name="service">The instance to destroy.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugServiceDestroy(_In_ JsDebugService service);

/// <summary>Register a handler instance with a given instance.</summary>
/// <param name="service">The instance to register with.</param>
/// <param name="id">The ID of the handler (it must be unique).</param>
/// <param name="handler">The handler instance.</param>
/// <param name="breakOnNextLine">Indicates whether to break on the next line of code.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugServiceRegisterHandler(
    _In_ JsDebugService service,
    _In_z_ const char* id,
    _In_ JsDebugProtocolHandler handler,
    _In_ bool breakOnNextLine);

/// <summary>Unregister a handler instance from a given instance.</summary>
/// <param name="service">The instance to unregister from.</param>
/// <param name="id">The ID of the handler to unregister.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugServiceUnregisterHandler(_In_ JsDebugService service, _In_z_ const char* id);

/// <summary>Start listening on a given port.</summary>
/// <param name="service">The instance to listen with.</param>
/// <param name="port">The port number to listen on.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugServiceListen(_In_ JsDebugService service, _In_ uint16_t port);

/// <summary>Stop listening and close any connections.</summary>
/// <param name="service">The instance to close.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugServiceClose(_In_ JsDebugService service);
