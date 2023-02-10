# Summary of Lab 3
It is `TCPSender`'s responsibility to keep tracking of the `TCPReceiver`
's window. and:
- Reading the bytes from `ByteStream`, creating new TCP segments, sendint them
to fill the window.
- `TCPSender` should keep sending segments until the window is full or the 
`ByteStream` has no elements.
- Record the segments that have been sent but not yet acknowledged by `TCPReceiver`.
(outstanding segments)
- When `timeout` passed, the outstanding segments which haven't been acknowledged 
should be resent.

## We need a `Retransmisstion Timer` class
A timer class is needed to implement by myself. So, two files `retransmission.hh`
and `retransmission.cc` are created. We should meet the following requirements:
- When `tick(const size_t ms_since_last_time)` is called, we need to add `ms_since_last_time`
to the `_accumulate_time`. If the accmulate time is greater than retransmission timeout(RTO)
, the timer has elapsed. A specific function `tick_callback` needs to be established
to check this situation.
- If the timer has elapsed, and the window size is not zero. We have to double the value of
`_rto`, aim to slow down the speed of retransmission. This is called `Congestion Control`.
- Every time a segment containing data is sent, if the timer is not running, start it running.
Here, I create a function called `start_timer` to achieve it.
- When receiver offers the sender an `ackno`, which means the receiver got the new byte 
successfully, then I use `reset_timer` to set the `_rto` to its initial value, and the
`_accumulate_time` is set to 0 at the same time. If all of outstanding segments have been
received, we should stop the timer. A function `stop_timer` is created to achieve this.

## TCP Sender
The main need is to implement four interfaces for `TcpSender`:
- void TCPSender::fill_window()
- void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size)
- void TCPSender::tick(const size_t ms_since_last_tick)
- void TCPSender::send_empty_segment()
`fill_window()` is to cut the ByteStream into TCPSegments, and the segments needs to
be pushed into `_segment_out`. Meantime a reference copy of segments needs to pushed
into `_outstanding_segments` before it is received by `TCPReceiver`.

More details to be considered:
- `TCPSender` should record the receiver's `ackno` and window size `_receiver_window_size`
to know whether we could transmit the new segments to the receiver.
- The ByteStream size is also needed to be considered.

In `full_window()`, `_outstanding_segments` should support insertion. In `ack_receiver()`,
when we received the `ackno` from the receiver, we update `_receiver_ack` firstly,
 then we delete the fully acknowledged segments from `_outstanding_segments` and calls
`fill_window` for the rest window size.

In `tick()`, it should check whether the retransmission timer has expired. If so, 
`TCPSender` needs to retransmit the earliest segment(the smallest sequence number).
Here, we decided use the data structure `List` to build as `_outstanding_segments`.

### Several private members
We can define the following fields:
- `_retransmission_timer`: retransmission timer mentioned above
- `_receive_ack`: the absolute receiver ack
- `_receiver_window_size`: the window size of `TCPReceiver` which should be set to 1
at first.
- `_outstanding_segments`: the outstanding segments mentioned above.
- `_consecutive_retransmission`: the number of consecutive retransmissions.
- `window_not_full()`: a function to tell if the window is not full.
```c++
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //! whether FIN has been sent
    bool end{};

    //! the retransimission timer
    RetransmissionTimer _retransmission_timer;

    //! the (absolute) receiver ack.
    uint64_t _receiver_ack{0};

    //! the initial window size should be 1
    uint64_t _receiver_window_size{1};

    //! the outstanding segments
    std::list<TCPSegment> _outstanding_segments{};

    //! the consecutive retransmissions
    unsigned int _consecutive_retransmissions{0};

    //! a helper function to tell whether the window is full
    bool window_not_full(uint64_t window_size) const { return window_size > bytes_in_flight(); }

  
};
```

### Important functions
#### fill_window()
Two cases need to be considered: 1. TCP connection case; 2. TCP disconnection case
For the TCP connection, where `_next_seqno == 0`, we should set the flag `syn` to be
true, then send the datagram out.

For the TCP disconnection, there are two more cases:
- An empty payload which indicates the disconnection where `stream_in().eof()`
is true.
- Nonempty payload which indicates the disconnection where `stream_in().eof()`is
true.
As to transferring bytes, we want to transmit as much as we want. However, the number of
transmitted bytes has its limits.
It is the minimum value of the following three elements: `TCPconfig::MAX_PAYLOAD_SIZE`, 
`_receiver_window_size - bytes_in_flight()` and `stream_in().buffer_size()`.
The first and the last element are easy to understand. As to the second, we should
consider not only the receiver's window size, but also those bytes we have sent but not
yet received by `TCPReceiver`.

More details can be seen in the code.
```c++
void TCPSender::fill_window() {
    // `FIN` has been sent
    if (end) {
        return;
    }

    TCPSegment segment{};
    // Special case: when the `_receiver_window_size` equals 0
    uint64_t window_size = _receiver_window_size == 0 ? 1 : _receiver_window_size;

    // Special case : TCP connection
    if (_next_seqno == 0) {
        segment.header().syn = true;
        segment.header().seqno = _isn + _next_seqno;
        _next_seqno += 1;
    } else {
        // Find the length to read from the `stream_in()`
        uint64_t length =
                std::min({window_size - bytes_in_flight(), stream_in().buffer_size(), TCPConfig::MAX_PAYLOAD_SIZE});
        segment.payload() = Buffer{stream_in().read(length)};
        segment.header().seqno = _isn + _next_seqno;
        _next_seqno += length;

        // When the `stream_in` is EOF,  we need to set the `fin` to `true`.
        // Pay attention, we should check whether there is an enough window size
        if (stream_in().eof() && window_not_full(window_size)) {
            segment.header().fin = true;
            end = true;
            _next_seqno++;
        }

        // do nothing
        if (length == 0 && !end)
            return;
    }
    segments_out().push(segment);
    _outstanding_segments.push_back(segment);
    _retransmission_timer.start_timer();

    if (window_not_full(window_size)) 
        fill_window();
}
```
#### ack_received()
The important thing is that TCP takes cumulative ack. When `TCPSender` receives
`ackno`, it means the segments whose number is less than `ackno` are all correctly
received by `TCPReceiver`.
The first thing when we get in this function is to check the special case: Is this
`ackno` a legal number? If not, we just return.
Then, we should step by step check the `_outstanding_segments`, if some elements'
sequence number is less than the `ackno` we receive, we should delete it from the
`_outstanding_segments`. However, there would be a corner case: the window size of 
receiver is zero:
> If the receiver has announced a window size of zero, the fill window method should
> act like the window size is one. The sender might end up sending a single byte that 
> gets rejected (and not acknowledged) by the receiver, but this can also provoke the 
> receiver into sending a new acknowledgment segment where it reveals that more space
> has opened up in its window. Without this, the sender would never learn that it was 
> allowed to start sending again.

In this case, the retransmission timeout should not be doubled.
```c++
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // When receiving unneeded ack, just return.
    uint64_t absolute_ack = unwrap(ackno, _isn, next_seqno_absolute());
    if (absolute_ack > _next_seqno || absolute_ack < _receiver_ack) return;

    _receiver_window_size = window_size;
    bool is_ack_update = false;

    auto iter = _outstanding_segments.begin();
    while (iter != _outstanding_segments.end()) {
        uint64_t sequence_num = unwrap(iter->header().seqno, _isn, next_seqno_absolute());
        if (sequence_num + iter->length_in_sequence_space() <= absolute_ack) {
            _receiver_ack = sequence_num + iter->length_in_sequence_space();
            iter = _outstanding_segments.erase(iter);
            is_ack_update = true;
        } else {
            iter++;
        }
    }

    // When there is no outstanding segments, we should stop the timer
    if (_outstanding_segments.empty()) {
        _retransmission_timer.stop_timer();
    }

    // When the receiver gives the sender an ackno that acknowledges
    // the successful receipt of new data
    if (is_ack_update) {
        _retransmission_timer.reset_timer();
        _consecutive_retransmissions = 0;
    }
}
```

#### tick()
This function needs to accumulate transmission time. When `_accumulate_time` 
exceeds `_rto` (retransmission timeout), we should double the `_rto` to control
the speed of retransmission except `_receiver_window_size` is 0. After that, we just
retransmit it.
```c++
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_retransmission_timer.tick_callback(ms_since_last_tick)) {
        if (_receiver_window_size == 0) {
            _retransmission_timer.reset_timer();
        } else {
            _retransmission_timer.handle_expired();
        }
        _consecutive_retransmissions++;
        segments_out().push(_outstanding_segments.front());
    }
}
```

#### send_empty_segment()
Easy operations:
```c++
void TCPSender::send_empty_segment() {
    TCPSegment empty{};
    empty.header().seqno = _isn + _next_seqno;
    segments_out().push(empty);
}
```