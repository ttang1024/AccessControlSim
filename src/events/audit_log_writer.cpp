#include "events/audit_log_writer.h"

#include <spdlog/spdlog.h>

namespace acs::events {

AuditLogWriter::AuditLogWriter(EventQueue& queue, IEventRepository& repository,
                               std::size_t maxBatch)
    : m_queue(queue), m_repository(repository), m_maxBatch(maxBatch), m_thread([this] { run(); }) {}

AuditLogWriter::~AuditLogWriter() { stop(); }

void AuditLogWriter::stop() {
    m_queue.close();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AuditLogWriter::run() {
    while (true) {
        const auto batch = m_queue.popBatch(m_maxBatch);
        if (batch.empty()) {
            return;  // Closed and drained.
        }
        if (!m_repository.append(batch)) {
            const std::size_t totalFailed = m_failedEvents += batch.size();
            spdlog::error("event=audit_write_failed events={} total_failed={}", batch.size(),
                          totalFailed);
        }
    }
}

}  // namespace acs::events
