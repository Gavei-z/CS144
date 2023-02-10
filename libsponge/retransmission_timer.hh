#ifndef SPONGE_LIBSPONGE_RETRANSMISSION_TIMER
#define SPONGE_LIBSPONGE_RETRANSMISSION_TIMER

#include <cstddef>

enum class TimerState { running, stop };

class RetransmissionTimer {
  private:
    TimerState state;             //! the state of the timer
    size_t _initial_rto;          //! the initial retransmission timeout
    size_t _rto;                  //! current retransmission timeout
    size_t _accumulate_time = 0;  //! the accumulate time
  public:
    //! constructor
    RetransmissionTimer(const size_t retx_timeout);

    //! check whether the time is expired
    bool tick_callback(const size_t ms_since_last_tick);

    //! when receiving a valid ack, reset the timer
    void reset_timer();

    //! when the timer is expired we should handle this situation
    void handle_expired();

    //! start the timer
    void start_timer();

    //! stop the timer
    void stop_timer();
};

#endif  // SPONGE_LIBSPONGE_RETRANSMISSION_TIMER
