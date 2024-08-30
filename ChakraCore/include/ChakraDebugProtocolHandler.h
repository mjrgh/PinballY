// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <ChakraCore.h>

typedef struct JsDebugProtocolHandler__* JsDebugProtocolHandler;
typedef void(CHAKRA_CALLBACK* JsDebugProtocolHandlerSendResponseCallback)(
    _In_z_ const char* response, 
    _In_opt_ void* callbackState);
typedef void(CHAKRA_CALLBACK* JsDebugProtocolHandlerCommandQueueCallback)(_In_opt_ void* callbackState);

/// <summary>Creates a <seealso cref="JsDebugProtocolHandler" /> instance for a given runtime.</summary>
/// <remarks>
///     It also implicitly enables debugging on the given runtime, so it will need to only be done when the engine is
///     not currently running script. This should be called before any code has been executed in the runtime.
/// </remarks>
/// <param name="runtime">The runtime to debug.</param>
/// <param name="protocolHandler">The newly created instance.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerCreate(_In_ JsRuntimeHandle runtime, _Out_ JsDebugProtocolHandler* protocolHandler);

/// <summary>Destroys the instance object.</summary>
/// <remarks>
///     It also implicitly disables debugging on the given runtime, so it will need to only be done when the engine is
///     not currently running script.
/// </remarks>
/// <param name="protocolHandler">The instance to destroy.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerDestroy(_In_ JsDebugProtocolHandler protocolHandler);

/// <summary>Connect a callback to the protocol handler.</summary>
/// <remarks>
///     Any events that occurred before connecting will be queued and dispatched upon successful connection.
/// </remarks>
/// <param name="protocolHandler">The instance to connect to.</param>
/// <param name="breakOnNextLine">Indicates whether to break on the next line of code.</param>
/// <param name="callback">The response callback function pointer.</param>
/// <param name="callbackState">The state object to return on each invocation of the callback.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerConnect(
    _In_ JsDebugProtocolHandler protocolHandler,
    _In_ bool breakOnNextLine,
    _In_ JsDebugProtocolHandlerSendResponseCallback callback,
    _In_opt_ void* callbackState);

/// <summary>Disconnect from the protocol handler and clear any breakpoints.</summary>
/// <param name="protocolHandler">The instance to disconnect from.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerDisconnect(_In_ JsDebugProtocolHandler protocolHandler);

/// <summary>Send an incoming JSON-formatted command to the protocol handler.</summary>
/// <remarks>
///     The response will be returned asynchronously.
/// </remarks>
/// <param name="protocolHandler">The receiving protocol handler.</param>
/// <param name="command">The JSON-formatted command to send.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerSendCommand(_In_ JsDebugProtocolHandler protocolHandler, _In_z_ const char* command);

/// <summay>Send a special request to the protocol handler.</summary>
/// <param name="protocolHandler">The receiving protocol handler.</parm>
/// <param name="request">The request to perform.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerSendRequest(_In_ JsDebugProtocolHandler protocolHandler, _In_z_ const char* request);

/// <summary>Generate a console API event.</summary>
/// <param name="type">Type of event (log, debug, info, error, warning, dir, dirxml, table, trace, clear, 
/// startGroup, startGroupCollapsed, endGroup, assert, profile, profileEnd, count, timeEnd)</param>
/// <param name="argv">Arguments</param>
/// <param name="argc">Number of arguments</param>
CHAKRA_API JsDebugConsoleAPIEvent(_In_ JsDebugProtocolHandler protocolHandler, _In_z_ const char* type, 
    _In_ const JsValueRef* argv, _In_ unsigned short argc);

/// <summary>Blocks the current thread until the debugger has connected.</summary>
/// <remarks>
///     This must be called from the script thread.
/// </remarks>
/// <param name="protocolHandler">The instance to wait on.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerWaitForDebugger(_In_ JsDebugProtocolHandler protocolHandler);

/// <summary>Processes any commands in the queue.</summary>
/// <remarks>
///     This must be called from the script thread.
/// </remarks>
/// <param name="protocolHandler">The instance to process.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerProcessCommandQueue(_In_ JsDebugProtocolHandler protocolHandler);

/// <summary>Registers a callback that notifies the host of any commands added to the queue.</summary>
/// <remarks>
///     This must be called from the script thread, but the callback can be called from any thread.
/// </remarks>
/// <param name="protocolHandler">The instance to register the callback on.</param>
/// <param name="callback">The command enqueued callback function pointer.</param>
/// <param name="callbackState">The state object to return on each invocation of the callback.</param>
/// <returns>The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.</returns>
CHAKRA_API JsDebugProtocolHandlerSetCommandQueueCallback(
    _In_ JsDebugProtocolHandler protocolHandler,
    _In_ JsDebugProtocolHandlerCommandQueueCallback callback,
    _In_opt_ void* callbackState);

/// <summary>Creeats and returns the objects which has console APIs popluated</summary>
/// <param name="protocolHandler">The instance to create object on.</param>
/// <param name="consoleObject">The populated console object</param>
CHAKRA_API JsDebugProtocolHandlerCreateConsoleObject(
    _In_ JsDebugProtocolHandler protocolHandler,
    _Out_ JsValueRef *consoleObject
);
