#ifndef _sys_Thread_h
#define _sys_Thread_h

/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */
#include <boost/shared_ptr.hpp>
#include "qpid/CommonImportExport.h"

#ifdef _WIN32
#  ifdef _MSC_VER
#    define QPID_TSS(type, name, default_value) __declspec(thread) type name = default_value
#  else
#    define QPID_TSS(type, name, default_value) __thread type name = default_value
#  endif
#elif defined (__GNUC__)
#  ifdef __MACH__
     #define QPID_TSS(type, name, default_value)							        \
static pthread_once_t tls_##name##_once_control = PTHREAD_ONCE_INIT;		        \
static pthread_key_t tls_##name##_key;						                        \
static struct 									                                    \
{										                                            \
public:										                                        \
  operator type& ()								                                    \
  {										                                            \
    return *GetType();								                                \
  }										                                            \
  void operator=(type value)							                            \
  {										                                            \
    (*GetType()) = value;							                                \
  }										                                            \
private:									                                        \
  static void MakeKey()								                                \
  {										                                            \
    pthread_key_create(&tls_##name##_key, NULL);				                    \
  }										                                            \
  type* GetType()								                                    \
  {										                                            \
    type* value = NULL;								                                \
    if (pthread_once(&tls_##name##_once_control, MakeKey) == 0)			            \
    {										                                        \
      value = reinterpret_cast < type * >(pthread_getspecific(tls_##name##_key));	\
      if (value == NULL)							                                \
      {										                                        \
        value = reinterpret_cast < type * >(malloc(sizeof(type)));		            \
        *value = default_value;                                                     \
        pthread_setspecific(tls_##name##_key, value);				                \
      }										                                        \
    }										                                        \
    return value;							                                    	\
  }										                                            \
} name
#  else
#    define QPID_TSS(type, name, default_value) __thread type name = default_value
#  endif
#elif defined (__SUNPRO_CC)
#  define QPID_TSS(type, name, default_value) __thread type name = default value
#else
#  error "Dont know how to define QPID_TSS for this platform"
#endif

namespace qpid {
namespace sys {

class Runnable;
class ThreadPrivate;

class Thread
{
    boost::shared_ptr<ThreadPrivate> impl;

  public:
    QPID_COMMON_EXTERN Thread();
    QPID_COMMON_EXTERN explicit Thread(qpid::sys::Runnable*);
    QPID_COMMON_EXTERN explicit Thread(qpid::sys::Runnable&);

    QPID_COMMON_EXTERN operator bool();
    QPID_COMMON_EXTERN bool operator==(const Thread&) const;
    QPID_COMMON_EXTERN bool operator!=(const Thread&) const;

    QPID_COMMON_EXTERN void join();

    QPID_COMMON_EXTERN static Thread current();

    /** ID of current thread for logging.
     * Workaround for broken Thread::current() in APR
     */
    QPID_COMMON_EXTERN static unsigned long logId();
};

}}
#endif  /*!_sys_Thread_h*/
