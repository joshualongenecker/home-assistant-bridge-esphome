/*!
 * @file
 * @brief Stub for esphome/core/defines.h — protocol_stack.h includes this
 *        header so the on-device define (GEA_BRIDGE_HAS_GEA2 /
 *        GEA_BRIDGE_HAS_GEA3, emitted by the codegen into the real
 *        defines.h) does not depend on an indirect include chain. The
 *        Makefile passes no GEA_BRIDGE defines, so protocol_stack.h's
 *        normalization block defines both and the test build compiles both
 *        protocol stacks (see protocol_stack.h).
 */
#pragma once
