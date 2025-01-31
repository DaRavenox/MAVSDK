#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "mavlink_address.h"
#include "mavlink_include.h"
#include "mavlink_message_handler.h"
#include "timeout_handler.h"
#include "locked_queue.h"

namespace mavsdk {

class Sender {
public:
    enum class Autopilot {
        Unknown,
        Px4,
        ArduPilot,
    };

    Sender() = default;
    virtual ~Sender() = default;
    virtual bool send_message(mavlink_message_t& message) = 0;
    [[nodiscard]] virtual uint8_t get_own_system_id() const = 0;
    [[nodiscard]] virtual uint8_t get_own_component_id() const = 0;
    [[nodiscard]] virtual uint8_t get_system_id() const = 0;
    [[nodiscard]] virtual Autopilot autopilot() const = 0;
};

class MAVLinkMissionTransfer {
public:
    enum class Result {
        Success,
        ConnectionError,
        Denied,
        TooManyMissionItems,
        Timeout,
        Unsupported,
        UnsupportedFrame,
        NoMissionAvailable,
        Cancelled,
        MissionTypeNotConsistent,
        InvalidSequence,
        CurrentInvalid,
        ProtocolError,
        InvalidParam,
        IntMessagesNotSupported
    };

    struct ItemInt {
        uint16_t seq;
        uint8_t frame;
        uint16_t command;
        uint8_t current;
        uint8_t autocontinue;
        float param1;
        float param2;
        float param3;
        float param4;
        int32_t x;
        int32_t y;
        float z;
        uint8_t mission_type;

        bool operator==(const ItemInt& other) const
        {
            return (
                seq == other.seq && frame == other.frame && command == other.command &&
                current == other.current && autocontinue == other.autocontinue &&
                param1 == other.param1 && param2 == other.param2 && param3 == other.param3 &&
                param4 == other.param4 && x == other.x && y == other.y && z == other.z &&
                mission_type == other.mission_type);
        }
    };

    using ResultCallback = std::function<void(Result result)>;
    using ResultAndItemsCallback = std::function<void(Result result, std::vector<ItemInt> items)>;
    using ProgressCallback = std::function<void(float progress)>;

    class WorkItem {
    public:
        explicit WorkItem(
            Sender& sender,
            MAVLinkMessageHandler& message_handler,
            TimeoutHandler& timeout_handler,
            uint8_t type,
            double timeout_s);
        virtual ~WorkItem();
        virtual void start() = 0;
        virtual void cancel() = 0;
        bool has_started();
        bool is_done();

        WorkItem(const WorkItem&) = delete;
        WorkItem(WorkItem&&) = delete;
        WorkItem& operator=(const WorkItem&) = delete;
        WorkItem& operator=(WorkItem&&) = delete;

    protected:
        Sender& _sender;
        MAVLinkMessageHandler& _message_handler;
        TimeoutHandler& _timeout_handler;
        uint8_t _type;
        double _timeout_s;
        bool _started{false};
        bool _done{false};
        std::mutex _mutex{};
    };

    class UploadWorkItem : public WorkItem {
    public:
        explicit UploadWorkItem(
            Sender& sender,
            MAVLinkMessageHandler& message_handler,
            TimeoutHandler& timeout_handler,
            uint8_t type,
            const std::vector<ItemInt>& items,
            double timeout_s,
            ResultCallback callback,
            ProgressCallback progress_callback);

        ~UploadWorkItem() override;
        void start() override;
        void cancel() override;

        UploadWorkItem(const UploadWorkItem&) = delete;
        UploadWorkItem(UploadWorkItem&&) = delete;
        UploadWorkItem& operator=(const UploadWorkItem&) = delete;
        UploadWorkItem& operator=(UploadWorkItem&&) = delete;

    private:
        void send_count();
        void send_mission_item();
        void send_cancel_and_finish();

        void process_mission_request(const mavlink_message_t& message);
        void process_mission_request_int(const mavlink_message_t& message);
        void process_mission_ack(const mavlink_message_t& message);
        void process_timeout();
        void callback_and_reset(Result result);

        void update_progress(float progress);

        enum class Step {
            SendCount,
            SendItems,
        } _step{Step::SendCount};

        std::vector<ItemInt> _items{};
        ResultCallback _callback{nullptr};
        ProgressCallback _progress_callback{nullptr};
        std::size_t _next_sequence{0};
        void* _cookie{nullptr};
        unsigned _retries_done{0};
    };

    class ReceiveIncomingMission : public WorkItem {
    public:
        explicit ReceiveIncomingMission(
            Sender& sender,
            MAVLinkMessageHandler& message_handler,
            TimeoutHandler& timeout_handler,
            uint8_t type,
            double timeout_s,
            ResultAndItemsCallback callback,
            uint32_t mission_count,
            uint8_t target_component);
        ~ReceiveIncomingMission() override;

        void start() override;
        void cancel() override;

        ReceiveIncomingMission(const ReceiveIncomingMission&) = delete;
        ReceiveIncomingMission(ReceiveIncomingMission&&) = delete;
        ReceiveIncomingMission& operator=(const ReceiveIncomingMission&) = delete;
        ReceiveIncomingMission& operator=(ReceiveIncomingMission&&) = delete;

    private:
        void request_item();
        void send_ack_and_finish();
        void send_cancel_and_finish();
        void process_mission_count();
        void process_mission_item_int(const mavlink_message_t& message);
        void process_timeout();
        void callback_and_reset(Result result);

        enum class Step {
            RequestList,
            RequestItem,
        } _step{Step::RequestList};

        std::vector<ItemInt> _items{};
        ResultAndItemsCallback _callback{nullptr};
        void* _cookie{nullptr};
        std::size_t _next_sequence{0};
        std::size_t _expected_count{0};
        unsigned _retries_done{0};
        uint32_t _mission_count{0};
        uint8_t _target_component{0};
    };

    class DownloadWorkItem : public WorkItem {
    public:
        explicit DownloadWorkItem(
            Sender& sender,
            MAVLinkMessageHandler& message_handler,
            TimeoutHandler& timeout_handler,
            uint8_t type,
            double timeout_s,
            ResultAndItemsCallback callback,
            ProgressCallback progress_callback);

        ~DownloadWorkItem() override;
        void start() override;
        void cancel() override;

        DownloadWorkItem(const DownloadWorkItem&) = delete;
        DownloadWorkItem(DownloadWorkItem&&) = delete;
        DownloadWorkItem& operator=(const DownloadWorkItem&) = delete;
        DownloadWorkItem& operator=(DownloadWorkItem&&) = delete;

    private:
        void request_list();
        void request_item();
        void send_ack_and_finish();
        void send_cancel_and_finish();
        void process_mission_count(const mavlink_message_t& message);
        void process_mission_item_int(const mavlink_message_t& message);
        void process_timeout();
        void callback_and_reset(Result result);

        void update_progress(float progress);

        enum class Step {
            RequestList,
            RequestItem,
        } _step{Step::RequestList};

        std::vector<ItemInt> _items{};
        ResultAndItemsCallback _callback{nullptr};
        ProgressCallback _progress_callback{nullptr};
        void* _cookie{nullptr};
        std::size_t _next_sequence{0};
        std::size_t _expected_count{0};
        unsigned _retries_done{0};
    };

    class ClearWorkItem : public WorkItem {
    public:
        ClearWorkItem(
            Sender& sender,
            MAVLinkMessageHandler& message_handler,
            TimeoutHandler& timeout_handler,
            uint8_t type,
            double timeout_s,
            ResultCallback callback);

        ~ClearWorkItem() override;
        void start() override;
        void cancel() override;

        ClearWorkItem(const ClearWorkItem&) = delete;
        ClearWorkItem(ClearWorkItem&&) = delete;
        ClearWorkItem& operator=(const ClearWorkItem&) = delete;
        ClearWorkItem& operator=(ClearWorkItem&&) = delete;

    private:
        void send_clear();
        void process_mission_ack(const mavlink_message_t& message);
        void process_timeout();
        void callback_and_reset(Result result);

        ResultCallback _callback{nullptr};
        void* _cookie{nullptr};
        unsigned _retries_done{0};
    };

    class SetCurrentWorkItem : public WorkItem {
    public:
        SetCurrentWorkItem(
            Sender& sender,
            MAVLinkMessageHandler& message_handler,
            TimeoutHandler& timeout_handler,
            int current,
            double timeout_s,
            ResultCallback callback);

        ~SetCurrentWorkItem() override;
        void start() override;
        void cancel() override;

        SetCurrentWorkItem(const SetCurrentWorkItem&) = delete;
        SetCurrentWorkItem(SetCurrentWorkItem&&) = delete;
        SetCurrentWorkItem& operator=(const SetCurrentWorkItem&) = delete;
        SetCurrentWorkItem& operator=(SetCurrentWorkItem&&) = delete;

    private:
        void send_current_mission_item();

        void process_mission_current(const mavlink_message_t& message);
        void process_timeout();
        void callback_and_reset(Result result);

        int _current{0};
        ResultCallback _callback{nullptr};
        void* _cookie{nullptr};
        unsigned _retries_done{0};
    };

    static constexpr unsigned retries = 5;

    using TimeoutSCallback = std::function<double()>;

    explicit MAVLinkMissionTransfer(
        Sender& sender,
        MAVLinkMessageHandler& message_handler,
        TimeoutHandler& timeout_handler,
        TimeoutSCallback get_timeout_s_callback);

    ~MAVLinkMissionTransfer() = default;

    std::weak_ptr<WorkItem> upload_items_async(
        uint8_t type,
        const std::vector<ItemInt>& items,
        const ResultCallback& callback,
        const ProgressCallback& progress_callback = nullptr);

    std::weak_ptr<WorkItem> download_items_async(
        uint8_t type,
        ResultAndItemsCallback callback,
        ProgressCallback progress_callback = nullptr);

    // Server-side
    std::weak_ptr<WorkItem> receive_incoming_items_async(
        uint8_t type,
        uint32_t mission_count,
        uint8_t target_component,
        ResultAndItemsCallback callback);

    void clear_items_async(uint8_t type, ResultCallback callback);

    void set_current_item_async(int current, ResultCallback callback);

    void do_work();
    bool is_idle();

    void set_int_messages_supported(bool supported);

    // Non-copyable
    MAVLinkMissionTransfer(const MAVLinkMissionTransfer&) = delete;
    const MAVLinkMissionTransfer& operator=(const MAVLinkMissionTransfer&) = delete;

private:
    Sender& _sender;
    MAVLinkMessageHandler& _message_handler;
    TimeoutHandler& _timeout_handler;
    TimeoutSCallback _timeout_s_callback;

    LockedQueue<WorkItem> _work_queue{};

    bool _int_messages_supported{true};
};

} // namespace mavsdk
