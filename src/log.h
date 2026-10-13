/*
 * Copyright 2026 Frank Hunleth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef LOG_H
#define LOG_H

void log_init(void);
void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

#define ERR_RETURN(MSG, ...) do { info(MSG, ## __VA_ARGS__); return -1; } while (0)
#define OK_OR_RETURN(WORK) do { if ((WORK) < 0) return -1; } while (0)
#define OK_OR_RETURN_MSG(WORK, MSG, ...) do { if ((WORK) < 0) ERR_RETURN(MSG, ## __VA_ARGS__); } while (0)
#define OK_OR_FATAL(WORK, MSG, ...) do { if ((WORK) < 0) fatal(MSG, ## __VA_ARGS__); } while (0)
#define OK_OR_WARN(WORK, MSG, ...) do { if ((WORK) < 0) info(MSG, ## __VA_ARGS__); } while (0)

#ifdef DEBUG
#define debug info
#else
#define debug(MSG, ...) do { } while (0)
#endif

#endif
