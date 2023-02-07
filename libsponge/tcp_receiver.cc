#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (seg.header().syn && !_isn.has_value()) {
//        _syn = true;
        _isn.emplace(seg.header().seqno);
//        _ack.emplace(_isn.value());
    }

    if (_isn.has_value()) {
        _reassembler.push_substring(
                seg.payload().copy(),
                unwrap(seg.header().seqno, _isn.value(), stream_out().bytes_written()),
                seg.header().fin
                );
        _ack.emplace(wrap(stream_out().bytes_written(), _isn.value()));

    }

    if (!_syn && seg.header().syn && _ack.has_value()) {
        _syn = true;
        _ack.emplace(_ack.value() + 1);
        _isn.emplace(_ack.value());
    }

    _fin = _fin ? _fin : seg.header().fin;
    if (_isn.has_value() && _fin && _reassembler.stream_out().input_ended()) {
        _ack = WrappingInt32(_ack.value() + 1);
    }

}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ack; }

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
