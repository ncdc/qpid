/*
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
*/

#include <windows.h>
#include <msclr\lock.h>
#include <oletx2xa.h>
#include <string>
#include <limits>

#include "qpid/messaging/Sender.h"
#include "qpid/messaging/Session.h"
#include "qpid/messaging/Message.h"

#include "Sender.h"
#include "Message.h"

namespace Org {
namespace Apache {
namespace Qpid {
namespace Messaging {

    /// <summary>
    /// Sender a managed wrapper for a ::qpid::messaging::Sender 
    /// </summary>

    // unmanaged clone
    Sender::Sender(const ::qpid::messaging::Sender & s,
                     Org::Apache::Qpid::Messaging::Session ^ sessRef) :
		senderp(new ::qpid::messaging::Sender (s)),
        parentSession(sessRef)
    {
    }


    // Destructor
    Sender::~Sender()
    {
        Cleanup();
    }


    // Finalizer
    Sender::!Sender()
    {
        Cleanup();
    }

    // Copy constructor
    Sender::Sender(const Sender ^ sender)
        : senderp(new ::qpid::messaging::Sender(
                        *(const_cast<Sender ^>(sender)->NativeSender))),
          parentSession(sender->parentSession)
    {
    }


    // Destroys kept object
    // TODO: add lock
    void Sender::Cleanup()
    {
        if (NULL != senderp)
        {
            delete senderp;
            senderp = NULL;
        }
    }

    //
    // Send(msg)
    //
    void Sender::Send(Message ^ mmsgp)
    {
        Send(mmsgp, false);
    }

    void Sender::Send(Message ^ mmsgp, bool sync)
    {
        senderp->::qpid::messaging::Sender::send(*((*mmsgp).NativeMessage), sync);
    }


    void Sender::Close()
    {
        senderp->close();
    }
}}}}