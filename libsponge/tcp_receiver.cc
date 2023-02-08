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
     * should be set.
     */
    if (seg.header().syn )
        _sender_isn.emplace(seg.header().seqno);

    /**
     * The function `bytes_written()` of the class `ByteStream` is always
     * pointing to the absolute current start window position. It is useful
     * when we push_substring(as the checkpoint).
     *
     * bytes_written() grows up from 1, but the index from 0.
     */
    if (_sender_isn.has_value()) {
        _reassembler.push_substring(
                seg.payload().copy(),
                unwrap(seg.header().seqno, _sender_isn.value(), stream_out().bytes_written()),
                seg.header().fin
                );
        _ack.emplace(wrap(stream_out().bytes_written(), _sender_isn.value()));

        /**
         * The payload is empty when the first SYN comes, and `_ack` == `_sender_isn`,
         * we should deal with this situation.
         */
         if (seg.header().syn) {
             _ack.emplace(_ack.value() + 1);
             _sender_isn.emplace(_ack.value());
         }

         /**
          * Only if we set up the connection and all the bytes were written into
          * the buffer in the `ByteStream`, we plus one to '_ack'.
          *
          * Because when closing the TCP connection, we receive the sender's seq number,
          * because of no payload, our `_ack` is equal to `seg.header().seqno`
          * Don't consider the sending, if we need the second FIN from the sender,
          * we should plus one to the `_ack` thus receiving the second FIN.
          */

         if (stream_out().input_ended())
             _ack = WrappingInt32(_ack.value() + 1);
    }

}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ack; }

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
