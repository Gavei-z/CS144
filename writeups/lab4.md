# CS144 lab4

Figure out this lab `TCPConnection`'s way to combine the `TCPReceiver` and `TCPSender` is quite necessary.

Here, an example is given to illustrate the process. I created two examples of `TCPConnection` which are `TCPConnectionA` and `TCPConnectionB`. 
Each one has its `sender` and `receiver`. If one peer(tcpA) sends segments to another(tcpB), `TCPSenderA`‘s `fill_window` method will be called to take out the data into the `_segments_out`. The absolute `seqno` will be kept for the next sent byte. But, How about the `ackno` of the header? We should handle this in the `TCPConnection`. 
When tcpB deal with the data it has received(call the method `segment_received()`). The following points need to be included:

- `TCPReceiverB` should use the method `segment_received` which updates its acknowledge-number which should later be sent to the `TCPConnectionA`. We can see: the acknowledge-number comes from itself receiver’s stored acknowledge number.

- Second, `TCPSenderB` should use `ack_received()` to update the `TCPSenderA`‘s acknowledge-number and its `window size`.
- Then, the `fill_window` method needs to be called, and we should pop the `TCPSenderB`‘s `_segments_out` to `TCPConnectionB`‘s `_segments_out`. Meantime, we need to change the header part, set the acknowledge-number to the `TCPReceiverB`‘s acknowledge-number.

## TCP Connection

Although it’s not hard to implement the code of sending the data or receiving the data, we need to handle the connection and close carefully.

### Connect

We should create a function called `send_empty_segment()` to produce a new segment when connecting.

### Close

The most challenging content is the way to close the `TCPConnection`. For unclean close, we just need to send (or receive) a segment with the `RST` flag.

However, for clean close. There are so a few things for considering.

There are four prerequisites to having a clean shutdown in its connection with the "remote" peer:

- > Prereq #1 The inbound stream has been fully assembled and has ended.

  A function is defined called `check_inbound_stream_assembled_and_ended()`.

  ```c++
  bool TCPConnection::check_inbound_stream_assembled_and_ended() { return _receiver.stream_out().eof(); }
  ```

- >  Prereq #2 The outbound stream has been ended by the local application and fully sent (including the fact that it ended, i.e. a segment with fin ) to the remote peer.

  A function is defined called `check_outbound_stream_ended_and_send_fin()` to ensure this situation.

  ```c++
  bool TCPConnection::check_outbound_stream_ended_and_send_fin() { return _sender.stream_in().eof() && _sender.is_end();} 
  ```

- > Prereq #3 The outbound stream has been fully acknowledged by the remote peer.

  Also, a function is defined as follow.

  ```c++
  bool TCPConnection::check_outbound_fully_acknowledged() { return _sender.bytes_in_flight() == 0; }
  ```

- > Prereq #4 The local `TCPConnection`is confident that the remote peer can satisfy prerequisite #3.

The first end connection peer should wait 10 times the initial retransmission timeout.

Because the remote peer may continue to send segments before getting `FIN`.

## Several helper functions

A function called `set_ack_and_window()` is defined firstly. It inspects the `_receiver`'s `ackno` and window size. Meantime it updates the `ack` field.

```c++
void TCPConnection::set_ack_and_window(TCPSegment &seg) {
    if (_receiver.ackno().has_value()) {
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
    }

    size_t window_size = _receiver.window_size();
    if (window_size > numeric_limits<uint16_t>::max())
        window_size = numeric_limits<uint16_t>::max();
    seg.header().win = window_size;
}
```

And `send_new_segments()` is also defined, which adds new segments to the `_segments_out`.

```c++
bool TCPConnection::send_new_segments() {
    bool is_really_send = false;
    while (!_sender.segments_out().empty()) {
        is_really_send = true;
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_window(segment);
        _segments_out.push(segment);
    }
    return is_really_send;
}
```

There two situations we need to send a segment with RST set. Thus a function named `send_rst_flag_segment()` is built up.

```c++
void TCPConnection::send_rst_flag_segment() {
    _sender.send_empty_segment(); // push the empty segment into the streams_out
    TCPSegment segment = _sender.segments_out().front();
    _sender.segments_out().pop();
    set_ack_and_window(segment);
    segment.header().rst = true;
    _segments_out.push(segment);
}
```

When it comes to set error, it should set both `_sender` and `_receiver` to be error, and the state `_active` to be false;

```c++
void TCPConnection::set_error() {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
}
```

## Connect

When the client wants to connect the server, it calls the `connect`. It is simple enough, because this job has been done in `TCPSender`.

```c++
void TCPConnection::connect() {
    _sender.fill_window();
    send_new_segments();
}

void TCPSender::fill_window() {
    ...
    // Special case : TCP connection
    if (_next_seqno == 0) {
        segment.header().syn = true;
        segment.header().seqno = _isn + _next_seqno;
        _next_seqno += 1;
    }
    ...
}
```

## segment_receive

This method, a few points need to be considered.

- We need to handle the segment with `RST`.
- `_receiver.segment_received()` method to update the `ackno` and window size.
- We need to check whether the inbound stream is end which is sent by the opposite sender, if so, we are the passive one, and we don't need the `TIME_WAIT` timer, set the `_linger_after_streams_finish` to be false.
- When the received segment has ACK set, we should first check out whether we should accept the segment. If the `receiver_ackno()` doesn’t exist, we just return. Otherwise, we should call `_sender.ack_received` and `_fill_window()` and calls `send_new_segments`.
- Special case: the segment could have no payload. We should deal with this situation.

```c++
void TCPConnection::segment_received(const TCPSegment &seg) {
    // reset the accumulated time
    _time_since_last_segment_received = 0;

    // if the `rst` flag is set, sets both the inbound and outbound
    // streams to the error state and kills the connection permanently
    if (seg.header().rst) {
        set_error();
        return;
    }

    // the receiver would update the `ackno` and window size of itself
    _receiver.segment_received(seg);

    // If the inbound stream ends before the `TCPConnection` has reached
    // EOF on its outbound stream, `_linger_after_streams_finish` should be false
    if (check_inbound_stream_assembled_and_ended() && !_sender.stream_in().eof())
        _linger_after_streams_finish = false;

    if (seg.header().ack) {
        if (!_receiver.ackno().has_value()) {
            // Corner case: when listening, we should drop all the ACK
            return;
        }

        _sender.ack_received(seg.header().ackno, seg.header().win);
        _sender.fill_window();
        send_new_segments();
    }
    
    // we need to handle the situation with no payload.
    if (seg.length_in_sequence_space() > 0) {
        _sender.fill_window();
        if (!send_new_segments()) {
            _sender.send_empty_segment();           // keep alive
            TCPSegment segment = _sender.segments_out().front();
            _sender.segments_out().pop();
            set_ack_and_window(segment);
            _segments_out.push(segment);
        }
    }
}
```

## write

Just implement it directly.

```c++
size_t TCPConnection::write(const string &data) {
    size_t len = _sender.stream_in().write(data); 
    _sender.fill_window();
    send_new_segments();
    return len;
}
```

## end_input_stream

This function is to make active close. So we first set the `ByteStream` of sender to be `end` and call `_fill_window`().

```c++
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_new_segments();
}
```

## tick

As to `tick`, we should retransmit the segment under some situations. When the counter `consecutive_retransmissions()` is greater than `_cfg.MAX_RETX_ATTEMPTS`, we should produce a segment with `RST` set.  When `tick` is called, for the passive one(who close the connection later), it just returns. For active one(the first one to close the connection) we need ensure the passive one received the `ACK` successfully sent by itself. But we can't ensure that, we decide if the passive one didn't resend the segment for a period of time, the connection should be closed.

```c++
//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    // retransmit the segments
    if (!_sender.segments_out().empty()) {
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_window(segment);
        if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
            set_error();
            segment.header().rst = true;
        }
        _segments_out.push(segment);
    }

    if (check_inbound_stream_assembled_and_ended() &&
        check_outbound_stream_ended_and_send_fin() &&
        check_outbound_fully_acknowledge()) {
        if (!_linger_after_streams_finish) _active = false;
        else if (_time_since_last_segment_received >= 10 * _cfg.rt_timeout)
            _active = false;
    }
}
```

