/*
 * Copyright (c) 2018 NITK Surathkal
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Vivek Jain <jain.vivek.anand@gmail.com>
 *          Viyom Mittal <viyommittal@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 */

#ifndef TCPBBR_H
#define TCPBBR_H

#include "tcp-congestion-ops.h"
#include "tcp-linux-reno.h"
#include "windowed-filter.h"

#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-value.h"

class TcpBbrCheckGainValuesTest;

namespace ns3
{

/**
 * @ingroup congestionOps
 *
 * @brief BBR congestion control algorithm
 *
 * This class implement the BBR (Bottleneck Bandwidth and Round-trip propagation time)
 * congestion control type.
 */
class TcpBbr : public TcpLinuxReno
{
  public:
    /**
     * @brief The number of phases in the BBR ProbeBW gain cycle.
     */
    static const uint8_t GAIN_CYCLE_LENGTH = 8;

    /**
     * @brief BBR uses an eight-phase cycle with the given pacing_gain value
     * in the BBR ProbeBW gain cycle.
     */
    const static double PACING_GAIN_CYCLE[];
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Constructor
     */
    TcpBbr();

    /**
     * Copy constructor.
     * @param sock The socket to copy from.
     */
    TcpBbr(const TcpBbr& sock);

    /**
     * @brief BBR has the following 4 modes for deciding how fast to send:
     */
    enum BbrMode_t
    {
        BBR_STARTUP,   /**< Ramp up sending rate rapidly to fill pipe */
        BBR_DRAIN,     /**< Drain any queue created during startup */
        BBR_PROBE_BW,  /**< Discover, share bw: pace around estimated bw */
        BBR_PROBE_RTT, /**< Cut inflight to min to probe min_rtt */
    };

    typedef WindowedFilter<DataRate,
                           MaxFilter<DataRate>,
                           uint32_t,
                           uint32_t>
        MaxBandwidthFilter_t; //!< Definition of max bandwidth filter.

    /**
     * @brief Literal names of BBR mode for use in log messages
     */
    static const char* const BbrModeName[BBR_PROBE_RTT + 1];

    /**
     * Assign a fixed random variable stream number to the random variables
     * used by this model.
     *
     * @param stream first stream index to use
     */
    virtual void SetStream(uint32_t stream);

    std::string GetName() const override;
    bool HasCongControl() const override;
    void CongControl(Ptr<TcpSocketState> tcb,
                     const TcpRateOps::TcpRateConnection& rc,
                     const TcpRateOps::TcpRateSample& rs) override;
    void CongestionStateSet(Ptr<TcpSocketState> tcb,
                            const TcpSocketState::TcpCongState_t newState) override;
    void CwndEvent(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event) override;
    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;
    Ptr<TcpCongestionOps> Fork() override;

    /**
     * @brief Set configuration required by congestion control algorithm,
     *        This method will force DctcpEcn mode and will force usage of
     *        either ECT(0) or ECT(1) (depending on the 'UseEct0' attribute),
     *        despite any other configuration in the base classes.
     *
     * @param tcb internal congestion state
     */
    void Init(Ptr<TcpSocketState> tcb) override;

    /**
     * TracedCallback signature for DCTCP update of congestion state
     *
     * @param [in] bytesAcked Bytes acked in this observation window
     * @param [in] bytesMarked Bytes marked in this observation window
     * @param [in] alpha New alpha (congestion estimate) value
     */
    typedef void (*CongestionEstimateTracedCallback)(uint32_t bytesAcked,
                                                     uint32_t bytesMarked,
                                                     double alpha);

    void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;

  public:
    /**
     * @brief TcpBbrCheckGainValuesTest friend class (for tests).
     * @relates TcpBbrCheckGainValuesTest
     */
    friend class ::TcpBbrCheckGainValuesTest;

    /**
     * @brief Advances pacing gain using cycle gain algorithm, while in BBR_PROBE_BW state
     */
    void AdvanceCyclePhase();

    /**
     * @brief Checks whether to advance pacing gain in BBR_PROBE_BW state,
     *  and if allowed calls AdvanceCyclePhase ()
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void CheckCyclePhase(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Checks whether its time to enter BBR_DRAIN or BBR_PROBE_BW state
     * @param tcb the socket state.
     */
    void CheckDrain(Ptr<TcpSocketState> tcb);

    /**
     * @brief Identifies whether pipe or BDP is already full
     * @param rs rate sample.
     */
    void CheckFullPipe(const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief This method handles the steps related to the ProbeRTT state
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void CheckProbeRTT(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Updates variables specific to BBR_DRAIN state
     */
    void EnterDrain();

    /**
     * @brief Updates variables specific to BBR_PROBE_BW state
     */
    void EnterProbeBW();

    /**
     * @brief Updates variables specific to BBR_PROBE_RTT state
     */
    void EnterProbeRTT();

    /**
     * @brief Updates variables specific to BBR_STARTUP state
     */
    void EnterStartup();

    /**
     * @brief Called on exiting from BBR_PROBE_RTT state, it eithers invoke EnterProbeBW () or
     * EnterStartup ()
     */
    void ExitProbeRTT();

    /**
     * @brief Gets BBR state.
     * @return returns BBR state.
     */
    uint32_t GetBbrState();

    /**
     * @brief Gets current pacing gain.
     * @return returns current pacing gain.
     */
    double GetPacingGain();

    /**
     * @brief Gets current cwnd gain.
     * @return returns current cwnd gain.
     */
    double GetCwndGain();

    /**
     * @brief Handles the steps for BBR_PROBE_RTT state.
     * @param tcb the socket state.
     */
    void HandleProbeRTT(Ptr<TcpSocketState> tcb);

    /**
     * @brief Updates pacing rate if socket is restarting from idle state.
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void HandleRestartFromIdle(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Estimates the target value for congestion window
     * @param tcb  the socket state.
     * @param gain cwnd gain.
     * @return returns congestion window based on max bandwidth and min RTT.
     */
    uint32_t InFlight(Ptr<TcpSocketState> tcb, double gain);

    /**
     * @brief Initializes the full pipe estimator.
     */
    void InitFullPipe();

    /**
     * @brief Initializes the pacing rate.
     * @param tcb  the socket state.
     */
    void InitPacingRate(Ptr<TcpSocketState> tcb);

    /**
     * @brief Initializes the round counting related variables.
     */
    void InitRoundCounting();

    /**
     * @brief Checks whether to move to next value of pacing gain while in BBR_PROBE_BW.
     * @param tcb the socket state.
     * @param rs  rate sample.
     * @returns true if want to move to next value otherwise false.
     */
    bool IsNextCyclePhase(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Modulates congestion window in BBR_PROBE_RTT.
     * @param tcb the socket state.
     */
    void ModulateCwndForProbeRTT(Ptr<TcpSocketState> tcb);

    /**
     * @brief Modulates congestion window in CA_RECOVERY.
     * @param tcb the socket state.
     * @param rs rate sample.
     * @return true if congestion window is updated in CA_RECOVERY.
     */
    bool ModulateCwndForRecovery(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Helper to restore the last-known good congestion window
     * @param tcb the socket state.
     */
    void RestoreCwnd(Ptr<TcpSocketState> tcb);

    /**
     * @brief Helper to remember the last-known good congestion window or
     *        the latest congestion window unmodulated by loss recovery or ProbeRTT.
     * @param tcb the socket state.
     */
    void SaveCwnd(Ptr<const TcpSocketState> tcb);

    /**
     * @brief Updates congestion window based on the network model.
     * @param tcb the socket state.
     * @param rs  rate sample
     */
    void SetCwnd(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Updates pacing rate based on network model.
     * @param tcb the socket state.
     * @param gain pacing gain.
     */
    void SetPacingRate(Ptr<TcpSocketState> tcb, double gain);

    /**
     * @brief Updates send quantum based on the network model.
     * @param tcb the socket state.
     */
    void SetSendQuantum(Ptr<TcpSocketState> tcb);

    /**
     * @brief Updates maximum bottleneck.
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void UpdateBottleneckBandwidth(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Updates control parameters congestion windowm, pacing rate, send quantum.
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void UpdateControlParameters(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Updates BBR network model (Maximum bandwidth and minimum RTT).
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void UpdateModelAndState(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Updates round counting related variables.
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void UpdateRound(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

    /**
     * @brief Updates minimum RTT.
     * @param tcb the socket state.
     */
    void UpdateRTprop(Ptr<TcpSocketState> tcb);

    /**
     * @brief Updates target congestion window.
     * @param tcb the socket state.
     */
    void UpdateTargetCwnd(Ptr<TcpSocketState> tcb);

    /**
     * @brief Sets BBR state.
     * @param state BBR state.
     */
    void SetBbrState(BbrMode_t state);

    /**
     * @brief Find Cwnd increment based on ack aggregation.
     * @return uint32_t aggregate cwnd.
     */
    uint32_t AckAggregationCwnd();

    /**
     * @brief Estimates max degree of aggregation.
     * @param tcb the socket state.
     * @param rs rate sample.
     */
    void UpdateAckAggregation(Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample& rs);

  private:
    BbrMode_t m_state{BbrMode_t::BBR_STARTUP}; //!< Current state of BBR state machine
    MaxBandwidthFilter_t m_maxBwFilter;        //!< Maximum bandwidth filter
    uint32_t m_bandwidthWindowLength{0}; //!< A constant specifying the length of the BBR.BtlBw max
                                         //!< filter window, default 10 packet-timed round trips.
    TracedValue<double> m_pacingGain{0}; //!< The dynamic pacing gain factor
    TracedValue<double> m_cWndGain{0};   //!< The dynamic congestion window gain factor
    double m_highGain{0};       //!< A constant specifying highest gain factor, default is 2.89
    bool m_isPipeFilled{false}; //!< A boolean that records whether BBR has filled the pipe
    uint32_t m_minPipeCwnd{
        0}; //!< The minimal congestion window value BBR tries to target, default 4 Segment size
    uint32_t m_roundCount{0}; //!< Count of packet-timed round trips
    bool m_roundStart{false}; //!< A boolean that BBR sets to true once per packet-timed round trip
    uint32_t m_nextRoundDelivered{0};           //!< Denotes the end of a packet-timed round trip
    Time m_probeRttDuration{MilliSeconds(200)}; //!< A constant specifying the minimum duration for
                                                //!< which ProbeRTT state, default 200 millisecs
    Time m_probeRtPropStamp;  //!< The wall clock time at which the current BBR.RTProp sample was
                              //!< obtained.
    Time m_probeRttDoneStamp; //!< Time to exit from BBR_PROBE_RTT state
    bool m_probeRttRoundDone{false};  //!< True when it is time to exit BBR_PROBE_RTT
    bool m_packetConservation{false}; //!< Enable/Disable packet conservation mode
    uint32_t m_priorCwnd{0};          //!< The last-known good congestion window
    bool m_idleRestart{false};        //!< When restarting from idle, set it true
    uint32_t m_targetCWnd{0}; //!< Target value for congestion window, adapted to the estimated BDP
    DataRate m_fullBandwidth{0};      //!< Value of full bandwidth recorded
    uint32_t m_fullBandwidthCount{0}; //!< Count of full bandwidth recorded consistently
    TracedValue<Time> m_minRtt{
        Time::Max()}; //!< Estimated two-way round-trip propagation delay of the path, estimated
                      //!< from the windowed minimum recent round-trip delay sample.
    uint32_t m_sendQuantum{
        0};            //!< The maximum size of a data aggregate scheduled and transmitted together
    Time m_cycleStamp; //!< Last time gain cycle updated
    uint32_t m_cycleIndex{0};            //!< Current index of gain cycle
    bool m_minRttExpired{false};         //!< A boolean recording whether the BBR.RTprop has expired
    Time m_minRttFilterLen{Seconds(10)}; //!< A constant specifying the length of the RTProp min
                                         //!< filter window, default 10 secs.
    Time m_minRttStamp; //!< The wall clock time at which the current BBR.RTProp sample was obtained
    bool m_isInitialized{false}; //!< Set to true after first time initialization variables
    Ptr<UniformRandomVariable> m_uv{nullptr}; //!< Uniform Random Variable
    uint64_t m_delivered{0}; //!< The total amount of data in bytes delivered so far
    uint32_t m_appLimited{
        0}; //!< The index of the last transmitted packet marked as application-limited
    uint32_t m_extraAckedGain{1};         //!< Gain factor for adding extra ack to cwnd
    uint32_t m_extraAcked[2]{0, 0};       //!< Maximum excess data acked in epoch
    uint32_t m_extraAckedWinRtt{0};       //!< Age of extra acked in rtt
    uint32_t m_extraAckedWinRttLength{5}; //!< Window length of extra acked window
    uint32_t m_ackEpochAckedResetThresh{
        1 << 17}; //!< Max allowed val for m_ackEpochAcked, after which sampling epoch is reset
    uint32_t m_extraAckedIdx{0}; //!< Current index in extra acked array
    Time m_ackEpochTime;         //!< Starting of ACK sampling epoch time
    uint32_t m_ackEpochAcked{0}; //!< Bytes ACked in sampling epoch
    bool m_hasSeenRtt{false};    //!< Have we seen RTT sample yet?
    double m_pacingMargin{0.01}; //!< BBR intentionally reduces the pacing rate by 1% to drain any
    //!< standing queues. See `bbr_rate_bytes_per_sec` in Linux.
    double m_rttJitter{0};

    /**
     * @brief Changes state of m_ceState to true
     *
     * @param tcb internal congestion state
     */
    void CeState0to1(Ptr<TcpSocketState> tcb);

    /**
     * @brief Changes state of m_ceState to false
     *
     * @param tcb internal congestion state
     */
    void CeState1to0(Ptr<TcpSocketState> tcb);

    /**
     * @brief Updates the value of m_delayedAckReserved
     *
     * @param tcb internal congestion state
     * @param event the congestion window event
     */
    void UpdateAckReserved(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event);

    /**
     * @brief Resets the value of m_ackedBytesEcn, m_ackedBytesTotal and m_nextSeq
     *
     * @param tcb internal congestion state
     */
    void Reset(Ptr<TcpSocketState> tcb);

    /**
     * @brief Initialize the value of m_alpha
     *
     * @param alpha DCTCP alpha parameter
     */
    void InitializeDctcpAlpha(double alpha);

    uint32_t m_ackedBytesEcn;       //!< Number of acked bytes which are marked
    uint32_t m_ackedBytesTotal;     //!< Total number of acked bytes
    SequenceNumber32 m_priorRcvNxt; //!< Sequence number of the first missing byte in data
    bool m_priorRcvNxtFlag; //!< Variable used in setting the value of m_priorRcvNxt for first time
    double m_alpha;         //!< Parameter used to estimate the amount of network congestion
    SequenceNumber32
        m_nextSeq;      //!< TCP sequence number threshold for beginning a new observation window
    bool m_nextSeqFlag; //!< Variable used in setting the value of m_nextSeq for first time
    bool m_ceState;     //!< DCTCP Congestion Experienced state
    bool m_delayedAckReserved; //!< Delayed Ack state
    double m_g;                //!< Estimation gain
    bool m_useEct0;            //!< Use ECT(0) for ECN codepoint
    bool m_initialized;        //!< Whether DCTCP has been initialized
    /**
     * @brief Callback pointer for congestion state update
     */
    TracedCallback<uint32_t, uint32_t, double> m_traceCongestionEstimate;
};

} // namespace ns3
#endif // TCPBBR_H
