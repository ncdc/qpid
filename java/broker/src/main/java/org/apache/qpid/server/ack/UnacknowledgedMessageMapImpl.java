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
package org.apache.qpid.server.ack;

import org.apache.qpid.server.store.StoreContext;
import java.util.Collection;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.apache.qpid.AMQException;
import org.apache.qpid.server.txn.TransactionalContext;

public class UnacknowledgedMessageMapImpl implements UnacknowledgedMessageMap
{
    private final Object _lock = new Object();

    private long _unackedSize;

    private Map<Long, UnacknowledgedMessage> _map;

    private long _lastDeliveryTag;

    private final int _prefetchLimit;

    public UnacknowledgedMessageMapImpl(int prefetchLimit)
    {
        _prefetchLimit = prefetchLimit;
        _map = new LinkedHashMap<Long, UnacknowledgedMessage>(prefetchLimit);
    }

    public void collect(Long deliveryTag, boolean multiple, List<UnacknowledgedMessage> msgs)
    {
        if (multiple)
        {
            collect(deliveryTag, msgs);
        }
        else
        {
            msgs.add(get(deliveryTag));
        }

    }

    public boolean contains(long deliveryTag) throws AMQException
    {
        synchronized (_lock)
        {
            return _map.containsKey(deliveryTag);
        }
    }

    public void remove(List<UnacknowledgedMessage> msgs)
    {
        synchronized (_lock)
        {
            for (UnacknowledgedMessage msg : msgs)
            {
                remove(msg.deliveryTag);
            }
        }
    }

    public UnacknowledgedMessage remove(long deliveryTag)
    {
        synchronized (_lock)
        {

            UnacknowledgedMessage message = _map.remove(deliveryTag);
            if(message != null)
            {
                _unackedSize -= message.getMessage().getSize();
            }

            return message;
        }
    }

    public void visit(Visitor visitor) throws AMQException
    {
        synchronized (_lock)
        {
            Collection<UnacknowledgedMessage> currentEntries = _map.values();
            for (UnacknowledgedMessage msg : currentEntries)
            {
                visitor.callback(msg);
            }
            visitor.visitComplete();
        }
    }

    public Object getLock()
    {
        return _lock;
    }

    public void add(long deliveryTag, UnacknowledgedMessage message)
    {
        synchronized (_lock)
        {
            _map.put(deliveryTag, message);
            _unackedSize += message.getMessage().getSize();
            _lastDeliveryTag = deliveryTag;
        }
    }

    public Collection<UnacknowledgedMessage> cancelAllMessages()
    {
        synchronized (_lock)
        {
            Collection<UnacknowledgedMessage> currentEntries = _map.values();
            _map = new LinkedHashMap<Long, UnacknowledgedMessage>(_prefetchLimit);
            _unackedSize = 0l;
            return currentEntries;
        }
    }

    public void acknowledgeMessage(long deliveryTag, boolean multiple, TransactionalContext txnContext)
            throws AMQException
    {
        synchronized (_lock)
        {
            txnContext.acknowledgeMessage(deliveryTag, _lastDeliveryTag, multiple, this);
        }
    }

    public int size()
    {
        synchronized (_lock)
        {
            return _map.size();
        }
    }

    public void clear()
    {
        synchronized (_lock)
        {
            _map.clear();
            _unackedSize = 0l;
        }
    }

    public void drainTo(long deliveryTag, StoreContext storeContext) throws AMQException
    {
        synchronized (_lock)
        {
            Iterator<Map.Entry<Long, UnacknowledgedMessage>> it = _map.entrySet().iterator();
            while (it.hasNext())
            {
                Map.Entry<Long, UnacknowledgedMessage> unacked = it.next();

                if (unacked.getKey() > deliveryTag)
                {
                    //This should not occur now.
                    throw new AMQException("UnacknowledgedMessageMap is out of order:" + unacked.getKey() +
                                           " When deliveryTag is:" + deliveryTag + "ES:" + _map.entrySet().toString());
                }

                //Message has been ack so discard it. This will dequeue and decrement the reference.
                unacked.getValue().discard(storeContext);

                it.remove();
                _unackedSize -= unacked.getValue().getMessage().getSize();

                if (unacked.getKey() == deliveryTag)
                {
                    break;
                }
            }
        }
    }
    
    public UnacknowledgedMessage get(long key)
    {
        synchronized (_lock)
        {
            return _map.get(key);
        }
    }

    public Set<Long> getDeliveryTags()
    {
        synchronized (_lock)
        {
            return _map.keySet();
        }
    }

    private void collect(Long key, List<UnacknowledgedMessage> msgs)
    {
        synchronized (_lock)
        {
            for (Map.Entry<Long, UnacknowledgedMessage> entry : _map.entrySet())
            {
                msgs.add(entry.getValue());
                if (entry.getKey().equals(key))
                {
                    break;
                }
            }
        }
    }

    public long getUnacknowledgeBytes()
    {
        return _unackedSize;
    }
}
