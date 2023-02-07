#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    /**
     * The first SYN is useful, the initial sequence number
     * should be set after that.
     */
    if (seg.header().syn && !_isn.has_value()) {
//        _syn = true;
        _isn.emplace(seg.header().seqno);
//        _ack.emplace(_isn.value());
    }

    /**
     * The function `bytes_written()` of the class `ByteStream` is always
     * pointing to the absolute current start window position. It is useful
     * when we push_substring(as the checkpoint).
     */
    if (_isn.has_value()) {
        _reassembler.push_substring(
                seg.payload().copy(),
                unwrap(seg.header().seqno, _isn.value(), stream_out().bytes_written()),
                seg.header().fin
                );
        _ack.emplace(wrap(stream_out().bytes_written(), _isn.value()));
    }

    /**
     * The payload is empty when the first SYN comes, and `_ack` == `_isn`,
     * we should deal with this situation.
     */
    if (!_syn && seg.header().syn && _ack.has_value()) {
        _syn = true;
        _ack.emplace(_ack.value() + 1);
        _isn.emplace(_ack.value());
    }

    /**
     * The connection was set up, the _fin == true, and all the bytes
     * were written, `_ack` plus 1. Cause the receiver need the second FIN
     * from the sender.
     */
    _fin = _fin ? _fin : seg.header().fin;
    if (_isn.has_value() && _fin && _reassembler.stream_out().input_ended()) {
        _ack = WrappingInt32(_ack.value() + 1);
    }

}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ack; }

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
