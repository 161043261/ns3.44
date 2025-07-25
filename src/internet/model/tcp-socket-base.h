/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
 * Copyright (c) 2010 Adrian Sai-wah Tam
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */
#ifndef TCP_SOCKET_BASE_H
#define TCP_SOCKET_BASE_H

#include "ipv4-header.h"
#include "ipv6-header.h"
#include "tcp-socket-state.h"
#include "tcp-socket.h"

#include "ns3/data-rate.h"
#include "ns3/node.h"
#include "ns3/sequence-number.h"
#include "ns3/timer.h"
#include "ns3/traced-value.h"

#include <shared_mutex>
#include <stdint.h>

namespace ns3
{

class Ipv4EndPoint;
class Ipv6EndPoint;
class Node;
class Packet;
class TcpL4Protocol;
class TcpHeader;
class TcpCongestionOps;
class TcpRecoveryOps;
class RttEstimator;
class TcpRxBuffer;
class TcpTxBuffer;
class TcpOption;
class Ipv4Interface;
class Ipv6Interface;
class TcpRateOps;

/**
 * @ingroup tcp
 *
 * @brief Helper class to store RTT measurements
 */
class RttHistory
{
  public:
    /**
     * @brief Constructor - builds an RttHistory with the given parameters
     * @param s First sequence number in packet sent
     * @param c Number of bytes sent
     * @param t Time this one was sent
     */
    RttHistory(SequenceNumber32 s, uint32_t c, Time t);
    /**
     * @brief Copy constructor
     * @param h the object to copy
     */
    RttHistory(const RttHistory& h); // Copy constructor
  public:
    SequenceNumber32 seq; //!< First sequence number in packet sent
    uint32_t count;       //!< Number of bytes sent
    Time time;            //!< Time this one was sent
    bool retx;            //!< True if this has been retransmitted
};

/**
 * @ingroup socket
 * @ingroup tcp
 *
 * @brief A base class for implementation of a stream socket using TCP.
 *
 * This class contains the essential components of TCP, as well as a sockets
 * interface for upper layers to call. This class provides connection orientation
 * and sliding window flow control; congestion control is delegated to subclasses
 * of TcpCongestionOps. Part of TcpSocketBase is modified from the original
 * NS-3 TCP socket implementation (TcpSocketImpl) by
 * Raj Bhattacharjea <raj.b@gatech.edu> of Georgia Tech.
 *
 * For IPv4 packets, the TOS set for the socket is used. The Bind and Connect
 * operations set the TOS for the socket to the value specified in the provided
 * address. A SocketIpTos tag is only added to the packet if the resulting
 * TOS is non-null.
 * Each packet is assigned the priority set for the socket. Setting a TOS
 * for a socket also sets a priority for the socket (according to the
 * Socket::IpTos2Priority function). A SocketPriority tag is only added to the
 * packet if the priority is non-null.
 *
 * Congestion state machine
 * ---------------------------
 *
 * The socket maintains two state machines; the TCP one, and another called
 * "Congestion state machine", which keeps track of the phase we are in. Currently,
 * ns-3 manages the states:
 *
 * - CA_OPEN
 * - CA_DISORDER
 * - CA_RECOVERY
 * - CA_LOSS
 * - CA_CWR
 *
 * For more information, see the TcpCongState_t documentation.
 *
 * Congestion control interface
 * ---------------------------
 *
 * Congestion control, unlike older releases of ns-3, has been split from
 * TcpSocketBase. In particular, each congestion control is now a subclass of
 * the main TcpCongestionOps class. Switching between congestion algorithm is
 * now a matter of setting a pointer into the TcpSocketBase class. The idea
 * and the interfaces are inspired by the Linux operating system, and in
 * particular from the structure tcp_congestion_ops. The reference paper is
 * https://www.sciencedirect.com/science/article/abs/pii/S1569190X15300939.
 *
 * Transmission Control Block (TCB)
 * --------------------------------
 *
 * The variables needed to congestion control classes to operate correctly have
 * been moved inside the TcpSocketState class. It contains information on the
 * congestion window, slow start threshold, segment size and the state of the
 * Congestion state machine.
 *
 * To track the trace inside the TcpSocketState class, a "forward" technique is
 * used, which consists in chaining callbacks from TcpSocketState to TcpSocketBase
 * (see for example cWnd trace source).
 *
 * Fast retransmit
 * ----------------
 *
 * The fast retransmit enhancement is introduced in RFC 2581 and updated in RFC
 * 5681. It reduces the time a sender waits before retransmitting a lost segment,
 * through the assumption that if it receives a certain number of duplicate ACKs,
 * a segment has been lost and it can be retransmitted. Usually, it is coupled
 * with the Limited Transmit algorithm, defined in RFC 3042. These algorithms
 * are included in this class, and they are implemented inside the ProcessAck
 * method. With the SACK option enabled, the LimitedTransmit algorithm will be
 * always on, as a consequence of how the information in the received SACK block
 * is managed.
 *
 * The attribute which manages the number of dup ACKs necessary to start the
 * fast retransmit algorithm is named "ReTxThreshold", and by default is 3.
 * The parameter is also used in TcpTxBuffer to determine if a packet is lost
 * (please take a look at TcpTxBuffer documentation to see details) but,
 * right now, it is assumed to be fixed. In future releases this parameter can
 * be made dynamic, to reflect the reordering degree of the network. With SACK,
 * the next sequence to transmit is given by the RFC 6675 algorithm. Without
 * SACK option, the implementation adds "hints" to TcpTxBuffer to make sure it
 * returns, as next transmittable sequence, the first lost (or presumed lost)
 * segment.
 *
 * Fast recovery
 * -------------
 *
 * The fast recovery algorithm is introduced RFC 2001, and it avoids to reset
 * cWnd to 1 segment after sensing a loss on the channel. Instead, a new slow
 * start threshold value is asked to the congestion control (for instance,
 * with NewReno the returned amount is half of the previous), and the cWnd is
 * set equal to such value. Ns-3 does not implement any inflation/deflation to
 * the congestion window since it uses an evolved method (borrowed from Linux
 * operating system) to calculate the number of bytes in flight. The fundamental
 * idea is to subtract from the total bytes in flight the lost/sacked amount
 * (the segments that have left the network) and to add the retransmitted count.
 * In this way, congestion window represents the exact number of bytes that
 * should be in flight. The implementation then decides what to transmit, it
 * there is space, between new or already transmitted data portion. If a value
 * of the congestion window with inflation and deflation is needed, there is a
 * traced source named "CongestionWindowInflated". However, the variable behind
 * it is not used in the code, but maintained for backward compatibility.
 *
 * RTO expiration
 * --------------
 *
 * When the Retransmission Time Out expires, the TCP faces a significant
 * performance drop. The expiration event is managed in the ReTxTimeout method,
 * which set the cWnd to 1 segment and starts "from scratch" again. The list
 * of sent packet is set as lost entirely, and the transmission is re-started
 * from the SND.UNA sequence number.
 *
 * Options management
 * ------------------
 *
 * SYN and SYN-ACK options, which are allowed only at the beginning of the
 * connection, are managed in the DoForwardUp and SendEmptyPacket methods.
 * To read all others, we have set up a cycle inside ReadOptions. For adding
 * them, there is no a unique place, since the options (and the information
 * available to build them) are scattered around the code. For instance,
 * the SACK option is built in SendEmptyPacket only under certain conditions.
 *
 * SACK
 * ----
 *
 * The SACK generation/management is delegated to the buffer classes, namely
 * TcpTxBuffer and TcpRxBuffer. In TcpRxBuffer it is managed the creation
 * of the SACK option from the receiver point of view. It must provide an
 * accurate (and efficient) representation of the status of the receiver buffer.
 * On the other side, inside TcpTxBuffer the received options (that contain
 * the SACK block) are processed and a particular data structure, called Scoreboard,
 * is filled. Please take a look at TcpTxBuffer and TcpRxBuffer documentation if
 * you need more information. The reference paper is
 * https://dl.acm.org/citation.cfm?id=3067666.
 *
 */
class TcpSocketBase : public TcpSocket
{
  public:
    /**
     * Get the type ID.
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Get the instance TypeId
     * @return the instance TypeId
     */
    TypeId GetInstanceTypeId() const override;

    /**
     * @brief TcpGeneralTest friend class (for tests).
     * @relates TcpGeneralTest
     */
    friend class TcpGeneralTest;

    /**
     * Create an unbound TCP socket
     */
    TcpSocketBase();

    /**
     * Clone a TCP socket, for use upon receiving a connection request in LISTEN state
     *
     * @param sock the original Tcp Socket
     */
    TcpSocketBase(const TcpSocketBase& sock);
    ~TcpSocketBase() override;

    /**
     * @brief Tcp Packet Types
     *
     * Taxonomy referred from Table 1 of
     * https://www.ietf.org/archive/id/draft-ietf-tcpm-generalized-ecn-15.txt
     */
    enum TcpPacketType_t
    {
        SYN,
        SYN_ACK,
        PURE_ACK,
        WINDOW_PROBE,
        FIN,
        RST,
        RE_XMT,
        DATA,
        INVALID
    };

    // Set associated Node, TcpL4Protocol, RttEstimator to this socket

    /**
     * @brief Set the associated node.
     * @param node the node
     */
    virtual void SetNode(Ptr<Node> node);

    /**
     * @brief Set the associated TCP L4 protocol.
     * @param tcp the TCP L4 protocol
     */
    virtual void SetTcp(Ptr<TcpL4Protocol> tcp);

    /**
     * @brief Set the associated RTT estimator.
     * @param rtt the RTT estimator
     */
    virtual void SetRtt(Ptr<RttEstimator> rtt);

    /**
     * @brief Sets the Minimum RTO.
     * @param minRto The minimum RTO.
     */
    void SetMinRto(Time minRto);

    /**
     * @brief Get the Minimum RTO.
     * @return The minimum RTO.
     */
    Time GetMinRto() const;

    /**
     * @brief Sets the Clock Granularity (used in RTO calcs).
     * @param clockGranularity The Clock Granularity
     */
    void SetClockGranularity(Time clockGranularity);

    /**
     * @brief Get the Clock Granularity (used in RTO calcs).
     * @return The Clock Granularity.
     */
    Time GetClockGranularity() const;

    /**
     * @brief Get a pointer to the Tx buffer
     * @return a pointer to the tx buffer
     */
    Ptr<TcpTxBuffer> GetTxBuffer() const;

    /**
     * @brief Get a pointer to the Rx buffer
     * @return a pointer to the rx buffer
     */
    Ptr<TcpRxBuffer> GetRxBuffer() const;

    /**
     * @brief Set the retransmission threshold (dup ack threshold for a fast retransmit)
     * @param retxThresh the threshold
     */
    void SetRetxThresh(uint32_t retxThresh);

    /**
     * @brief Get the retransmission threshold (dup ack threshold for a fast retransmit)
     * @return the threshold
     */
    uint32_t GetRetxThresh() const
    {
        return m_retxThresh;
    }

    /**
     * @brief Callback pointer for pacing rate trace chaining
     */
    TracedCallback<DataRate, DataRate> m_pacingRateTrace;

    /**
     * @brief Callback pointer for cWnd trace chaining
     */
    TracedCallback<uint32_t, uint32_t> m_cWndTrace;

    /**
     * @brief Callback pointer for cWndInfl trace chaining
     */
    TracedCallback<uint32_t, uint32_t> m_cWndInflTrace;

    /**
     * @brief Callback pointer for ssTh trace chaining
     */
    TracedCallback<uint32_t, uint32_t> m_ssThTrace;

    /**
     * @brief Callback pointer for congestion state trace chaining
     */
    TracedCallback<TcpSocketState::TcpCongState_t, TcpSocketState::TcpCongState_t> m_congStateTrace;

    /**
     * @brief Callback pointer for ECN state trace chaining
     */
    TracedCallback<TcpSocketState::EcnState_t, TcpSocketState::EcnState_t> m_ecnStateTrace;

    /**
     * @brief Callback pointer for high tx mark chaining
     */
    TracedCallback<SequenceNumber32, SequenceNumber32> m_highTxMarkTrace;

    /**
     * @brief Callback pointer for next tx sequence chaining
     */
    TracedCallback<SequenceNumber32, SequenceNumber32> m_nextTxSequenceTrace;

    /**
     * @brief Callback pointer for bytesInFlight trace chaining
     */
    TracedCallback<uint32_t, uint32_t> m_bytesInFlightTrace;

    /**
     * @brief Callback pointer for RTT trace chaining
     */
    TracedCallback<Time, Time> m_srttTrace;

    /**
     * @brief Callback pointer for Last RTT trace chaining
     */
    TracedCallback<Time, Time> m_lastRttTrace;

    /**
     * @brief Callback function to hook to TcpSocketState pacing rate
     * @param oldValue old pacing rate value
     * @param newValue new pacing rate value
     */
    void UpdatePacingRateTrace(DataRate oldValue, DataRate newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState congestion window
     * @param oldValue old cWnd value
     * @param newValue new cWnd value
     */
    void UpdateCwnd(uint32_t oldValue, uint32_t newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState inflated congestion window
     * @param oldValue old cWndInfl value
     * @param newValue new cWndInfl value
     */
    void UpdateCwndInfl(uint32_t oldValue, uint32_t newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState slow start threshold
     * @param oldValue old ssTh value
     * @param newValue new ssTh value
     */
    void UpdateSsThresh(uint32_t oldValue, uint32_t newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState congestion state
     * @param oldValue old congestion state value
     * @param newValue new congestion state value
     */
    void UpdateCongState(TcpSocketState::TcpCongState_t oldValue,
                         TcpSocketState::TcpCongState_t newValue) const;

    /**
     * @brief Callback function to hook to EcnState state
     * @param oldValue old ecn state value
     * @param newValue new ecn state value
     */
    void UpdateEcnState(TcpSocketState::EcnState_t oldValue,
                        TcpSocketState::EcnState_t newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState high tx mark
     * @param oldValue old high tx mark
     * @param newValue new high tx mark
     */
    void UpdateHighTxMark(SequenceNumber32 oldValue, SequenceNumber32 newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState next tx sequence
     * @param oldValue old nextTxSeq value
     * @param newValue new nextTxSeq value
     */
    void UpdateNextTxSequence(SequenceNumber32 oldValue, SequenceNumber32 newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState bytes inflight
     * @param oldValue old bytesInFlight value
     * @param newValue new bytesInFlight value
     */
    void UpdateBytesInFlight(uint32_t oldValue, uint32_t newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState rtt
     * @param oldValue old rtt value
     * @param newValue new rtt value
     */
    void UpdateRtt(Time oldValue, Time newValue) const;

    /**
     * @brief Callback function to hook to TcpSocketState lastRtt
     * @param oldValue old lastRtt value
     * @param newValue new lastRtt value
     */
    void UpdateLastRtt(Time oldValue, Time newValue) const;

    /**
     * @brief Install a congestion control algorithm on this socket
     *
     * @param algo Algorithm to be installed
     */
    void SetCongestionControlAlgorithm(Ptr<TcpCongestionOps> algo);

    /**
     * @brief Install a recovery algorithm on this socket
     *
     * @param recovery Algorithm to be installed
     */
    void SetRecoveryAlgorithm(Ptr<TcpRecoveryOps> recovery);

    /**
     * @brief Mark ECT(0) codepoint
     *
     * @param tos the TOS byte to modify
     * @return TOS with ECT(0) codepoint set
     */
    inline uint8_t MarkEcnEct0(uint8_t tos) const
    {
        return ((tos & 0xfc) | 0x02);
    }

    /**
     * @brief Mark ECT(1) codepoint
     *
     * @param tos the TOS byte to modify
     * @return TOS with ECT(1) codepoint set
     */
    inline uint8_t MarkEcnEct1(uint8_t tos) const
    {
        return ((tos & 0xfc) | 0x01);
    }

    /**
     * @brief Mark CE codepoint
     *
     * @param tos the TOS byte to modify
     * @return TOS with CE codepoint set
     */
    inline uint8_t MarkEcnCe(uint8_t tos) const
    {
        return ((tos & 0xfc) | 0x03);
    }

    /**
     * @brief Clears ECN bits from TOS
     *
     * @param tos the TOS byte to modify
     * @return TOS without ECN bits
     */
    inline uint8_t ClearEcnBits(uint8_t tos) const
    {
        return tos & 0xfc;
    }

    /**
     * @brief Checks if TOS has no ECN codepoints
     *
     * @param tos the TOS byte to check
     * @return true if TOS does not have any ECN codepoints set; otherwise false
     */
    inline bool CheckNoEcn(uint8_t tos) const
    {
        return ((tos & 0x03) == 0x00);
    }

    /**
     * @brief Checks for ECT(0) codepoint
     *
     * @param tos the TOS byte to check
     * @return true if TOS has ECT(0) codepoint set; otherwise false
     */
    inline bool CheckEcnEct0(uint8_t tos) const
    {
        return ((tos & 0x03) == 0x02);
    }

    /**
     * @brief Checks for ECT(1) codepoint
     *
     * @param tos the TOS byte to check
     * @return true if TOS has ECT(1) codepoint set; otherwise false
     */
    inline bool CheckEcnEct1(uint8_t tos) const
    {
        return ((tos & 0x03) == 0x01);
    }

    /**
     * @brief Checks for CE codepoint
     *
     * @param tos the TOS byte to check
     * @return true if TOS has CE codepoint set; otherwise false
     */
    inline bool CheckEcnCe(uint8_t tos) const
    {
        return ((tos & 0x03) == 0x03);
    }

    /**
     * @brief mark ECN code point
     *
     * @param tos the TOS byte to modify
     * @param codePoint the codepoint to use
     * @return TOS with specified ECN code point
     */
    inline uint8_t MarkEcnCodePoint(const uint8_t tos,
                                    const TcpSocketState::EcnCodePoint_t codePoint) const
    {
        return ((tos & 0xfc) | codePoint);
    }

    /**
     * @brief Set ECN mode of use on the socket
     *
     * @param useEcn Mode of ECN to use.
     */
    void SetUseEcn(TcpSocketState::UseEcn_t useEcn);

    /**
     * @brief Enable or disable pacing
     * @param pacing Boolean to enable or disable pacing
     */
    void SetPacingStatus(bool pacing);

    /**
     * @brief Enable or disable pacing of the initial window
     * @param paceWindow Boolean to enable or disable pacing of the initial window
     */
    void SetPaceInitialWindow(bool paceWindow);

    /**
     * @brief Checks if a TCP packet should be ECN-capable (ECT) according to the TcpPacketType and
     * ECN mode.
     *
     * Currently, only Classic ECN and DCTCP ECN modes are supported, with
     * potential extensions for future modes (Ecnpp).
     *
     * @param packetType The type of the TCP packet, represented by an enum TcpPacketType.
     * @return true if the packet is ECN-capable (ECT), false otherwise.
     *
     * Reference: https://www.ietf.org/archive/id/draft-ietf-tcpm-generalized-ecn-15.txt (Table 1)
     */
    bool IsEct(TcpPacketType_t packetType) const;

    // Necessary implementations of null functions from ns3::Socket
    SocketErrno GetErrno() const override;     // returns m_errno
    SocketType GetSocketType() const override; // returns socket type
    Ptr<Node> GetNode() const override;        // returns m_node
    int Bind() override;  // Bind a socket by setting up endpoint in TcpL4Protocol
    int Bind6() override; // Bind a socket by setting up endpoint in TcpL4Protocol
    int Bind(const Address& address) override; // ... endpoint of specific addr or port
    int Connect(
        const Address& address) override; // Setup endpoint and call ProcessAction() to connect
    int Listen()
        override; // Verify the socket is in a correct state and call ProcessAction() to listen
    int Close() override;        // Close by app: Kill socket upon tx buffer emptied
    int ShutdownSend() override; // Assert the m_shutdownSend flag to prevent send to network
    int ShutdownRecv() override; // Assert the m_shutdownRecv flag to prevent forward to app
    int Send(Ptr<Packet> p, uint32_t flags) override; // Call by app to send data to network
    int SendTo(Ptr<Packet> p,
               uint32_t flags,
               const Address& toAddress) override; // Same as Send(), toAddress is insignificant
    Ptr<Packet> Recv(uint32_t maxSize,
                     uint32_t flags) override; // Return a packet to be forwarded to app
    Ptr<Packet> RecvFrom(uint32_t maxSize, uint32_t flags, Address& fromAddress)
        override;                             // ... and write the remote address at fromAddress
    uint32_t GetTxAvailable() const override; // Available Tx buffer size
    uint32_t GetRxAvailable()
        const override; // Available-to-read data size, i.e. value of m_rxAvailable
    int GetSockName(Address& address) const override; // Return local addr:port in address
    int GetPeerName(Address& address) const override;
    void BindToNetDevice(Ptr<NetDevice> netdevice) override; // NetDevice with my m_endPoint

    /**
     * TracedCallback signature for TCP packet transmission or reception events.
     *
     * @param [in] packet The packet.
     * @param [in] header The TcpHeader
     * @param [in] socket This socket
     */
    typedef void (*TcpTxRxTracedCallback)(const Ptr<const Packet> packet,
                                          const TcpHeader& header,
                                          const Ptr<const TcpSocketBase> socket);

    /**
     * TracedCallback signature for TCP packet retransmission events.
     *
     * @param [in] packet The packet.
     * @param [in] header The TcpHeader
     * @param [in] localAddr The local address
     * @param [in] peerAddr The peer/remote address
     * @param [in] socket This socket
     */
    typedef void (*RetransmissionCallback)(const Ptr<const Packet> packet,
                                           const TcpHeader& header,
                                           const Address& localAddr,
                                           const Address& peerAddr,
                                           const Ptr<const TcpSocketBase> socket);

  protected:
    // Implementing ns3::TcpSocket -- Attribute get/set
    // inherited, no need to doc

    void SetSndBufSize(uint32_t size) override;
    uint32_t GetSndBufSize() const override;
    void SetRcvBufSize(uint32_t size) override;
    uint32_t GetRcvBufSize() const override;
    void SetSegSize(uint32_t size) override;
    uint32_t GetSegSize() const override;
    void SetInitialSSThresh(uint32_t threshold) override;
    uint32_t GetInitialSSThresh() const override;
    void SetInitialCwnd(uint32_t cwnd) override;
    uint32_t GetInitialCwnd() const override;
    void SetConnTimeout(Time timeout) override;
    Time GetConnTimeout() const override;
    void SetSynRetries(uint32_t count) override;
    uint32_t GetSynRetries() const override;
    void SetDataRetries(uint32_t retries) override;
    uint32_t GetDataRetries() const override;
    void SetDelAckTimeout(Time timeout) override;
    Time GetDelAckTimeout() const override;
    void SetDelAckMaxCount(uint32_t count) override;
    uint32_t GetDelAckMaxCount() const override;
    void SetTcpNoDelay(bool noDelay) override;
    bool GetTcpNoDelay() const override;
    void SetPersistTimeout(Time timeout) override;
    Time GetPersistTimeout() const override;
    bool SetAllowBroadcast(bool allowBroadcast) override;
    bool GetAllowBroadcast() const override;

    // Helper functions: Connection set up

    /**
     * @brief Common part of the two Bind(), i.e. set callback and remembering local addr:port
     *
     * @returns 0 on success, -1 on failure
     */
    int SetupCallback();

    /**
     * @brief Perform the real connection tasks: Send SYN if allowed, RST if invalid
     *
     * @returns 0 on success
     */
    int DoConnect();

    /**
     * @brief Schedule-friendly wrapper for Socket::NotifyConnectionSucceeded()
     */
    void ConnectionSucceeded();

    /**
     * @brief Configure the endpoint to a local address. Called by Connect() if Bind() didn't
     * specify one.
     *
     * @returns 0 on success
     */
    int SetupEndpoint();

    /**
     * @brief Configure the endpoint v6 to a local address. Called by Connect() if Bind() didn't
     * specify one.
     *
     * @returns 0 on success
     */
    int SetupEndpoint6();

    /**
     * @brief Complete a connection by forking the socket
     *
     * This function is called only if a SYN received in LISTEN state. After
     * TcpSocketBase cloned, allocate a new end point to handle the incoming
     * connection and send a SYN+ACK to complete the handshake.
     *
     * @param p the packet triggering the fork
     * @param tcpHeader the TCP header of the triggering packet
     * @param fromAddress the address of the remote host
     * @param toAddress the address the connection is directed to
     */
    virtual void CompleteFork(Ptr<Packet> p,
                              const TcpHeader& tcpHeader,
                              const Address& fromAddress,
                              const Address& toAddress);

    // Helper functions: Transfer operation

    /**
     * @brief Checks whether the given TCP segment is valid or not.
     *
     * @param seq the sequence number of packet's TCP header
     * @param tcpHeaderSize the size of packet's TCP header
     * @param tcpPayloadSize the size of TCP payload
     * @return true if the TCP segment is valid
     */
    bool IsValidTcpSegment(const SequenceNumber32 seq,
                           const uint32_t tcpHeaderSize,
                           const uint32_t tcpPayloadSize);

    /**
     * @brief Called by the L3 protocol when it received a packet to pass on to TCP.
     *
     * @param packet the incoming packet
     * @param header the packet's IPv4 header
     * @param port the remote port
     * @param incomingInterface the incoming interface
     */
    void ForwardUp(Ptr<Packet> packet,
                   Ipv4Header header,
                   uint16_t port,
                   Ptr<Ipv4Interface> incomingInterface);

    /**
     * @brief Called by the L3 protocol when it received a packet to pass on to TCP.
     *
     * @param packet the incoming packet
     * @param header the packet's IPv6 header
     * @param port the remote port
     * @param incomingInterface the incoming interface
     */
    void ForwardUp6(Ptr<Packet> packet,
                    Ipv6Header header,
                    uint16_t port,
                    Ptr<Ipv6Interface> incomingInterface);

    /**
     * @brief Called by TcpSocketBase::ForwardUp{,6}().
     *
     * Get a packet from L3. This is the real function to handle the
     * incoming packet from lower layers. This is
     * wrapped by ForwardUp() so that this function can be overloaded by daughter
     * classes.
     *
     * @param packet the incoming packet
     * @param fromAddress the address of the sender of packet
     * @param toAddress the address of the receiver of packet (hopefully, us)
     */
    virtual void DoForwardUp(Ptr<Packet> packet,
                             const Address& fromAddress,
                             const Address& toAddress);

    /**
     * @brief Called by the L3 protocol when it received an ICMP packet to pass on to TCP.
     *
     * @param icmpSource the ICMP source address
     * @param icmpTtl the ICMP Time to Live
     * @param icmpType the ICMP Type
     * @param icmpCode the ICMP Code
     * @param icmpInfo the ICMP Info
     */
    void ForwardIcmp(Ipv4Address icmpSource,
                     uint8_t icmpTtl,
                     uint8_t icmpType,
                     uint8_t icmpCode,
                     uint32_t icmpInfo);

    /**
     * @brief Called by the L3 protocol when it received an ICMPv6 packet to pass on to TCP.
     *
     * @param icmpSource the ICMP source address
     * @param icmpTtl the ICMP Time to Live
     * @param icmpType the ICMP Type
     * @param icmpCode the ICMP Code
     * @param icmpInfo the ICMP Info
     */
    void ForwardIcmp6(Ipv6Address icmpSource,
                      uint8_t icmpTtl,
                      uint8_t icmpType,
                      uint8_t icmpCode,
                      uint32_t icmpInfo);

    /**
     * @brief Send as much pending data as possible according to the Tx window.
     *
     * Note that this function did not implement the PSH flag.
     *
     * @param withAck forces an ACK to be sent
     * @returns the number of packets sent
     */
    uint32_t SendPendingData(bool withAck = false);

    /**
     * @brief Extract at most maxSize bytes from the TxBuffer at sequence seq, add the
     *        TCP header, and send to TcpL4Protocol
     *
     * @param seq the sequence number
     * @param maxSize the maximum data block to be transmitted (in bytes)
     * @param withAck forces an ACK to be sent
     * @returns the number of bytes sent
     */
    virtual uint32_t SendDataPacket(SequenceNumber32 seq, uint32_t maxSize, bool withAck);

    /**
     * @brief Send a empty packet that carries a flag, e.g., ACK
     *
     * @param flags the packet's flags
     */
    virtual void SendEmptyPacket(uint8_t flags);

    /**
     * @brief Send reset and tear down this socket
     */
    void SendRST();

    /**
     * @brief Check if a sequence number range is within the rx window
     *
     * @param head start of the Sequence window
     * @param tail end of the Sequence window
     * @returns true if it is in range
     */
    bool OutOfRange(SequenceNumber32 head, SequenceNumber32 tail) const;

    // Helper functions: Connection close

    /**
     * @brief Close a socket by sending RST, FIN, or FIN+ACK, depend on the current state
     *
     * @returns 0 on success
     */
    int DoClose();

    /**
     * @brief Peacefully close the socket by notifying the upper layer and deallocate end point
     */
    void CloseAndNotify();

    /**
     * @brief Kill this socket by zeroing its attributes (IPv4)
     *
     * This is a callback function configured to m_endpoint in
     * SetupCallback(), invoked when the endpoint is destroyed.
     */
    void Destroy();

    /**
     * @brief Kill this socket by zeroing its attributes (IPv6)
     *
     * This is a callback function configured to m_endpoint in
     * SetupCallback(), invoked when the endpoint is destroyed.
     */
    void Destroy6();

    /**
     * @brief Deallocate m_endPoint and m_endPoint6
     */
    void DeallocateEndPoint();

    /**
     * @brief Received a FIN from peer, notify rx buffer
     *
     * @param p the packet
     * @param tcpHeader the packet's TCP header
     */
    void PeerClose(Ptr<Packet> p, const TcpHeader& tcpHeader);

    /**
     * @brief FIN is in sequence, notify app and respond with a FIN
     */
    void DoPeerClose();

    /**
     * @brief Cancel all timer when endpoint is deleted
     */
    void CancelAllTimers();

    /**
     * @brief Move from CLOSING or FIN_WAIT_2 to TIME_WAIT state
     */
    void TimeWait();

    // State transition functions

    /**
     * @brief Received a packet upon ESTABLISHED state.
     *
     * This function is mimicking the role of tcp_rcv_established() in tcp_input.c in Linux kernel.
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    void ProcessEstablished(Ptr<Packet> packet,
                            const TcpHeader& tcpHeader); // Received a packet upon ESTABLISHED state

    /**
     * @brief Received a packet upon LISTEN state.
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     * @param fromAddress the source address
     * @param toAddress the destination address
     */
    void ProcessListen(Ptr<Packet> packet,
                       const TcpHeader& tcpHeader,
                       const Address& fromAddress,
                       const Address& toAddress);

    /**
     * @brief Received a packet upon SYN_SENT
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    void ProcessSynSent(Ptr<Packet> packet, const TcpHeader& tcpHeader);

    /**
     * @brief Received a packet upon SYN_RCVD.
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     * @param fromAddress the source address
     * @param toAddress the destination address
     */
    void ProcessSynRcvd(Ptr<Packet> packet,
                        const TcpHeader& tcpHeader,
                        const Address& fromAddress,
                        const Address& toAddress);

    /**
     * @brief Received a packet upon CLOSE_WAIT, FIN_WAIT_1, FIN_WAIT_2
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    void ProcessWait(Ptr<Packet> packet, const TcpHeader& tcpHeader);

    /**
     * @brief Received a packet upon CLOSING
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    void ProcessClosing(Ptr<Packet> packet, const TcpHeader& tcpHeader);

    /**
     * @brief Received a packet upon LAST_ACK
     *
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    void ProcessLastAck(Ptr<Packet> packet, const TcpHeader& tcpHeader);

    // Window management

    /**
     * @brief Return count of number of unacked bytes
     *
     * The difference between SND.UNA and HighTx
     *
     * @returns count of number of unacked bytes
     */
    virtual uint32_t UnAckDataCount() const;

    /**
     * @brief Return total bytes in flight
     *
     * Does not count segments lost and SACKed (or dupACKed)
     *
     * @returns total bytes in flight
     */
    virtual uint32_t BytesInFlight() const;

    /**
     * @brief Return the max possible number of unacked bytes
     * @returns the max possible number of unacked bytes
     */
    virtual uint32_t Window() const;

    /**
     * @brief Return unfilled portion of window
     * @return unfilled portion of window
     */
    virtual uint32_t AvailableWindow() const;

    /**
     * @brief The amount of Rx window announced to the peer
     * @param scale indicate if the window should be scaled. True for
     * almost all cases, except when we are sending a SYN
     * @returns size of Rx window announced to the peer
     */
    virtual uint16_t AdvertisedWindowSize(bool scale = true) const;

    /**
     * @brief Update the receiver window (RWND) based on the value of the
     * window field in the header.
     *
     * This method suppresses updates unless one of the following three
     * conditions holds:  1) segment contains new data (advancing the right
     * edge of the receive buffer), 2) segment does not contain new data
     * but the segment acks new data (highest sequence number acked advances),
     * or 3) the advertised window is larger than the current send window
     *
     * @param header TcpHeader from which to extract the new window value
     */
    void UpdateWindowSize(const TcpHeader& header);

    // Manage data tx/rx

    /**
     * @brief Call CopyObject<> to clone me
     * @returns a copy of the socket
     */
    virtual Ptr<TcpSocketBase> Fork();

    /**
     * @brief Received an ACK packet
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    virtual void ReceivedAck(Ptr<Packet> packet, const TcpHeader& tcpHeader);

    /**
     * @brief Process a received ack
     * @param ackNumber ack number
     * @param scoreboardUpdated if true indicates that the scoreboard has been
     * @param oldHeadSequence value of HeadSequence before ack
     * updated with SACK information
     * @param currentDelivered The number of bytes (S)ACKed
     * @param receivedData if true indicates that data is piggybacked with ACK
     */
    virtual void ProcessAck(const SequenceNumber32& ackNumber,
                            bool scoreboardUpdated,
                            uint32_t currentDelivered,
                            const SequenceNumber32& oldHeadSequence,
                            bool receivedData);

    /**
     * @brief Recv of a data, put into buffer, call L7 to get it if necessary
     * @param packet the packet
     * @param tcpHeader the packet's TCP header
     */
    virtual void ReceivedData(Ptr<Packet> packet, const TcpHeader& tcpHeader);

    /**
     * @brief Calculate RTT sample for the ACKed packet
     *
     * Per RFC 6298 (Section 3),
     * If `m_timestampsEnabled` is true, calculate RTT using timestamps option.
     * Otherwise, return RTT as the elapsed time since the packet was transmitted.
     * If ACKed packed was a retrasmitted packet, return zero time.
     *
     * @param tcpHeader the packet's TCP header
     * @param rttHistory the ACKed packet's RTT History
     * @returns the RTT sample
     */
    virtual Time CalculateRttSample(const TcpHeader& tcpHeader, const RttHistory& rttHistory);

    /**
     * @brief Take into account the packet for RTT estimation
     * @param tcpHeader the packet's TCP header
     */
    virtual void EstimateRtt(const TcpHeader& tcpHeader);

    /**
     * @brief Update the RTT history, when we send TCP segments
     *
     * @param seq The sequence number of the TCP segment
     * @param sz The segment's size
     * @param isRetransmission Whether or not the segment is a retransmission
     */

    virtual void UpdateRttHistory(const SequenceNumber32& seq, uint32_t sz, bool isRetransmission);

    /**
     * @brief Update buffers w.r.t. ACK
     * @param seq the sequence number
     * @param resetRTO indicates if RTO should be reset
     */
    virtual void NewAck(const SequenceNumber32& seq, bool resetRTO);

    /**
     * @brief Dupack management
     *
     * @param currentDelivered Current (S)ACKed bytes
     */
    void DupAck(uint32_t currentDelivered);

    /**
     * @brief Enter CA_CWR state upon receipt of an ECN Echo
     *
     * @param currentDelivered Currently (S)ACKed bytes
     */
    void EnterCwr(uint32_t currentDelivered);

    /**
     * @brief Enter the CA_RECOVERY, and retransmit the head
     *
     * @param currentDelivered Currently (S)ACKed bytes
     */
    void EnterRecovery(uint32_t currentDelivered);

    /**
     * @brief An RTO event happened
     */
    virtual void ReTxTimeout();

    /**
     * @brief Action upon delay ACK timeout, i.e. send an ACK
     */
    virtual void DelAckTimeout();

    /**
     * @brief Timeout at LAST_ACK, close the connection
     */
    virtual void LastAckTimeout();

    /**
     * @brief Send 1 byte probe to get an updated window size
     */
    virtual void PersistTimeout();

    /**
     * @brief Retransmit the first segment marked as lost, without considering
     * available window nor pacing.
     */
    void DoRetransmit();

    /**
     * @brief Add options to TcpHeader
     *
     * Test each option, and if it is enabled on our side, add it
     * to the header
     *
     * @param tcpHeader TcpHeader to add options to
     */
    void AddOptions(TcpHeader& tcpHeader);

    /**
     * @brief Read TCP options before Ack processing
     *
     * Timestamp and Window scale are managed in other pieces of code.
     *
     * @param tcpHeader Header of the segment
     * @param [out] bytesSacked Number of bytes SACKed, or 0
     */
    void ReadOptions(const TcpHeader& tcpHeader, uint32_t* bytesSacked);

    /**
     * @brief Return true if the specified option is enabled
     *
     * @param kind kind of TCP option
     * @return true if the option is enabled
     */
    bool IsTcpOptionEnabled(uint8_t kind) const;

    /**
     * @brief Read and parse the Window scale option
     *
     * Read the window scale option (encoded logarithmically) and save it.
     * Per RFC 1323, the value can't exceed 14.
     *
     * @param option Window scale option read from the header
     */
    void ProcessOptionWScale(const Ptr<const TcpOption> option);
    /**
     * @brief Add the window scale option to the header
     *
     * Calculate our factor from the rxBuffer max size, and add it
     * to the header.
     *
     * @param header TcpHeader where the method should add the window scale option
     */
    void AddOptionWScale(TcpHeader& header);

    /**
     * @brief Calculate window scale value based on receive buffer space
     *
     * Calculate our factor from the rxBuffer max size
     *
     * @returns the Window Scale factor
     */
    uint8_t CalculateWScale() const;

    /**
     * @brief Read the SACK PERMITTED option
     *
     * Currently this is a placeholder, since no operations should be done
     * on such option.
     *
     * @param option SACK PERMITTED option from the header
     */
    void ProcessOptionSackPermitted(const Ptr<const TcpOption> option);

    /**
     * @brief Read the SACK option
     *
     * @param option SACK option from the header
     * @returns the number of bytes sacked by this option
     */
    uint32_t ProcessOptionSack(const Ptr<const TcpOption> option);

    /**
     * @brief Add the SACK PERMITTED option to the header
     *
     * @param header TcpHeader where the method should add the option
     */
    void AddOptionSackPermitted(TcpHeader& header);

    /**
     * @brief Add the SACK option to the header
     *
     * @param header TcpHeader where the method should add the option
     */
    void AddOptionSack(TcpHeader& header);

    /**
     * @brief Process the timestamp option from other side
     *
     * Get the timestamp and the echo, then save timestamp (which will
     * be the echo value in our out-packets) and save the echoed timestamp,
     * to utilize later to calculate RTT.
     *
     * @see EstimateRtt
     * @param option Option from the segment
     * @param seq Sequence number of the segment
     */
    void ProcessOptionTimestamp(const Ptr<const TcpOption> option, const SequenceNumber32& seq);
    /**
     * @brief Add the timestamp option to the header
     *
     * Set the timestamp as the lower bits of the Simulator::Now time,
     * and the echo value as the last seen timestamp from the other part.
     *
     * @param header TcpHeader to which add the option to
     */
    void AddOptionTimestamp(TcpHeader& header);

    /**
     * @brief Performs a safe subtraction between a and b (a-b)
     *
     * Safe is used to indicate that, if b>a, the results returned is 0.
     *
     * @param a first number
     * @param b second number
     * @return 0 if b>0, (a-b) otherwise
     */
    static uint32_t SafeSubtraction(uint32_t a, uint32_t b);

    /**
     * @brief Notify Pacing
     */
    void NotifyPacingPerformed();

    /**
     * @brief Return true if packets in the current window should be paced
     * @return true if pacing is currently enabled
     */
    bool IsPacingEnabled() const;

    /**
     * @brief Dynamically update the pacing rate
     */
    void UpdatePacingRate();

    /**
     * @brief Add Tags for the Socket
     * @param p Packet
     * @param isEct Whether the packet is allowed to be ECT capable
     */
    void AddSocketTags(const Ptr<Packet>& p, bool isEct) const;

    /**
     * Get the current value of the receiver's offered window (RCV.WND)
     * @note This method exists to expose the value to the TcpTxBuffer
     * @return value of receiver's offered window
     */
    uint32_t GetRWnd() const;

    /**
     * Get the current value of the receiver's highest (in-sequence) sequence number acked.
     * @note This method exists to expose the value to the TcpTxBuffer
     * @return value of receiver's highest sequence number acked.
     */
    SequenceNumber32 GetHighRxAck() const;

  protected:
    // Counters and events
    EventId m_retxEvent{};     //!< Retransmission event
    EventId m_lastAckEvent{};  //!< Last ACK timeout event
    EventId m_delAckEvent{};   //!< Delayed ACK timeout event
    EventId m_persistEvent{};  //!< Persist event: Send 1 byte to probe for a non-zero Rx window
    EventId m_timewaitEvent{}; //!< TIME_WAIT expiration event: Move this socket to CLOSED state

    // ACK management
    uint32_t m_dupAckCount{0};    //!< Dupack counter
    uint32_t m_delAckCount{0};    //!< Delayed ACK counter
    uint32_t m_delAckMaxCount{0}; //!< Number of packet to fire an ACK before delay timeout

    // Nagle algorithm
    bool m_noDelay{false}; //!< Set to true to disable Nagle's algorithm

    // Retries
    uint32_t m_synCount{0};      //!< Count of remaining connection retries
    uint32_t m_synRetries{0};    //!< Number of connection attempts
    uint32_t m_dataRetrCount{0}; //!< Count of remaining data retransmission attempts
    uint32_t m_dataRetries{0};   //!< Number of data retransmission attempts

    // Timeouts
    TracedValue<Time> m_rto;                 //!< Retransmit timeout
    Time m_minRto{Time::Max()};              //!< minimum value of the Retransmit timeout
    Time m_clockGranularity{Seconds(0.001)}; //!< Clock Granularity used in RTO calcs
    Time m_delAckTimeout;                    //!< Time to delay an ACK
    Time m_persistTimeout;                   //!< Time between sending 1-byte probes
    Time m_cnTimeout;                        //!< Timeout for connection retry

    // History of RTT
    std::deque<RttHistory> m_history; //!< List of sent packet

    // Connections to other layers of TCP/IP
    Ipv4EndPoint* m_endPoint{nullptr};  //!< the IPv4 endpoint
    Ipv6EndPoint* m_endPoint6{nullptr}; //!< the IPv6 endpoint
    Ptr<Node> m_node;                   //!< the associated node
    Ptr<TcpL4Protocol> m_tcp;           //!< the associated TCP L4 protocol
    Callback<void, Ipv4Address, uint8_t, uint8_t, uint8_t, uint32_t>
        m_icmpCallback; //!< ICMP callback
    Callback<void, Ipv6Address, uint8_t, uint8_t, uint8_t, uint32_t>
        m_icmpCallback6; //!< ICMPv6 callback

    Ptr<RttEstimator> m_rtt; //!< Round trip time estimator

    // Tx buffer management
    Ptr<TcpTxBuffer> m_txBuffer; //!< Tx buffer

    // State-related attributes
    TracedValue<TcpStates_t> m_state{CLOSED}; //!< TCP state

    mutable SocketErrno m_errno{ERROR_NOTERROR}; //!< Socket error code

    bool m_closeNotified{false}; //!< Told app to close socket
    bool m_closeOnEmpty{false};  //!< Close socket upon tx buffer emptied
    bool m_shutdownSend{false};  //!< Send no longer allowed
    bool m_shutdownRecv{false};  //!< Receive no longer allowed
    bool m_connected{false};     //!< Connection established
    double m_msl{0.0};           //!< Max segment lifetime

    // Window management
    uint16_t m_maxWinSize{0};                         //!< Maximum window size to advertise
    uint32_t m_bytesAckedNotProcessed{0};             //!< Bytes acked, but not processed
    SequenceNumber32 m_highTxAck{0};                  //!< Highest ack sent
    TracedValue<uint32_t> m_rWnd{0};                  //!< Receiver window (RCV.WND in RFC793)
    TracedValue<uint32_t> m_advWnd{0};                //!< Advertised Window size
    TracedValue<SequenceNumber32> m_highRxMark{0};    //!< Highest seqno received
    TracedValue<SequenceNumber32> m_highRxAckMark{0}; //!< Highest ack received

    // Options
    bool m_sackEnabled{true};       //!< RFC SACK option enabled
    bool m_winScalingEnabled{true}; //!< Window Scale option enabled (RFC 7323)
    uint8_t m_rcvWindShift{0};      //!< Window shift to apply to outgoing segments
    uint8_t m_sndWindShift{0};      //!< Window shift to apply to incoming segments
    bool m_timestampEnabled{true};  //!< Timestamp option enabled
    uint32_t m_timestampToEcho{0};  //!< Timestamp to echo

    EventId m_sendPendingDataEvent{}; //!< micro-delay event to send pending data

    // Fast Retransmit and Recovery
    SequenceNumber32 m_recover{
        0}; //!< Previous highest Tx seqnum for fast recovery (set it to initial seq number)
    bool m_recoverActive{false}; //!< Whether "m_recover" has been set/activated
                                 //!< It is used to avoid comparing with the old m_recover value
                                 //!< which was set for handling previous congestion event.
    uint32_t m_retxThresh{3};    //!< Fast Retransmit threshold
    bool m_limitedTx{true};      //!< perform limited transmit

    // Transmission Control Block
    Ptr<TcpSocketState> m_tcb;                 //!< Congestion control information
    Ptr<TcpCongestionOps> m_congestionControl; //!< Congestion control
    Ptr<TcpRecoveryOps> m_recoveryOps;         //!< Recovery Algorithm
    Ptr<TcpRateOps> m_rateOps;                 //!< Rate operations

    // Guesses over the other connection end
    bool m_isFirstPartialAck{true}; //!< First partial ACK during RECOVERY

    // The following three traces pass a packet with a TCP header
    TracedCallback<Ptr<const Packet>,
                   const TcpHeader&,
                   Ptr<const TcpSocketBase>>
        m_txTrace; //!< Trace of transmitted packets

    TracedCallback<Ptr<const Packet>,
                   const TcpHeader&,
                   const Address&,
                   const Address&,
                   Ptr<const TcpSocketBase>>
        m_retransmissionTrace; //!< Trace of retransmitted packets

    TracedCallback<Ptr<const Packet>,
                   const TcpHeader&,
                   Ptr<const TcpSocketBase>>
        m_rxTrace; //!< Trace of received packets

    // Pacing related variable
    Timer m_pacingTimer{Timer::CANCEL_ON_DESTROY}; //!< Pacing Event

    // Parameters related to Explicit Congestion Notification
    TracedValue<SequenceNumber32> m_ecnEchoSeq{
        0}; //!< Sequence number of the last received ECN Echo
    TracedValue<SequenceNumber32> m_ecnCESeq{
        0}; //!< Sequence number of the last received Congestion Experienced
    TracedValue<SequenceNumber32> m_ecnCWRSeq{0}; //!< Sequence number of the last sent CWR
};

/**
 * @ingroup tcp
 * TracedValue Callback signature for TcpCongState_t
 *
 * @param [in] oldValue original value of the traced variable
 * @param [in] newValue new value of the traced variable
 */
typedef void (*TcpCongStatesTracedValueCallback)(const TcpSocketState::TcpCongState_t oldValue,
                                                 const TcpSocketState::TcpCongState_t newValue);

/**
 * @ingroup tcp
 * TracedValue Callback signature for ECN state trace
 *
 * @param [in] oldValue original value of the traced variable
 * @param [in] newValue new value of the traced variable
 */
typedef void (*EcnStatesTracedValueCallback)(const TcpSocketState::EcnState_t oldValue,
                                             const TcpSocketState::EcnState_t newValue);

} // namespace ns3

class RttCache
{
  public:
    static RttCache& Instance()
    {
        static RttCache instance;
        return instance;
    }

    void PushRtt(ns3::Time rtt)
    {
        std::lock_guard<std::shared_mutex> lock(m_mut);
        if (m_rttDeque.size() >= 10)
        {
            m_rttDeque.pop_front();
        }
        m_rttDeque.push_back(rtt);
    }

    std::deque<ns3::Time> GetRttDeque()
    {
        std::lock_guard<std::shared_mutex> lock(m_mut);
        return m_rttDeque;
    }

  private:
    RttCache() = default;
    std::deque<ns3::Time> m_rttDeque;
    std::shared_mutex m_mut;
};

#endif /* TCP_SOCKET_BASE_H */
