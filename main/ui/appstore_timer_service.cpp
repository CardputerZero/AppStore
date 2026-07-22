#include "appstore_timer_service.hpp"

#include <utility>

namespace appstore_ui {

AppStoreTimerService::AppStoreTimerService(Callbacks callbacks)
    : callbacks_(std::move(callbacks))
{
}

AppStoreTimerService::~AppStoreTimerService()
{
    stop();
}

void AppStoreTimerService::start(uint32_t refresh_ms, uint32_t package_job_ms,
                                 uint32_t esc_hold_ms, uint32_t sync_ms)
{
    stop();
    sync_timer_ = lv_timer_create(sync_callback, sync_ms, this);
    refresh_timer_ = lv_timer_create(refresh_callback, refresh_ms, this);
    esc_hold_timer_ = lv_timer_create(esc_hold_callback, esc_hold_ms, this);
    package_job_timer_ = lv_timer_create(package_job_callback, package_job_ms, this);
}

void AppStoreTimerService::stop()
{
    if (esc_hold_timer_) lv_timer_delete(esc_hold_timer_);
    if (refresh_timer_) lv_timer_delete(refresh_timer_);
    if (package_job_timer_) lv_timer_delete(package_job_timer_);
    if (sync_timer_) lv_timer_delete(sync_timer_);
    esc_hold_timer_ = nullptr;
    refresh_timer_ = nullptr;
    package_job_timer_ = nullptr;
    sync_timer_ = nullptr;
}

void AppStoreTimerService::refresh_callback(lv_timer_t *timer)
{
    auto *self = static_cast<AppStoreTimerService *>(lv_timer_get_user_data(timer));
    if (self && self->callbacks_.refresh) self->callbacks_.refresh();
}

void AppStoreTimerService::package_job_callback(lv_timer_t *timer)
{
    auto *self = static_cast<AppStoreTimerService *>(lv_timer_get_user_data(timer));
    if (self && self->callbacks_.package_job) self->callbacks_.package_job();
}

void AppStoreTimerService::esc_hold_callback(lv_timer_t *timer)
{
    auto *self = static_cast<AppStoreTimerService *>(lv_timer_get_user_data(timer));
    if (self && self->callbacks_.esc_hold) self->callbacks_.esc_hold();
}

void AppStoreTimerService::sync_callback(lv_timer_t *timer)
{
    auto *self = static_cast<AppStoreTimerService *>(lv_timer_get_user_data(timer));
    if (self && self->callbacks_.sync) self->callbacks_.sync();
}

} // namespace appstore_ui
