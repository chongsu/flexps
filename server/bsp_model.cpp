#include "server/bsp_model.hpp"
#include "glog/logging.h"

namespace flexps {

BSPModel::BSPModel(uint32_t model_id, std::unique_ptr<AbstractStorage>&& storage_ptr,
                   ThreadsafeQueue<Message>* reply_queue, int dump_interval)
    : model_id_(model_id), reply_queue_(reply_queue) {
  this->storage_ = std::move(storage_ptr);
  this->dump_interval_ = dump_interval;
  LOG(INFO) << "[BSPModel] Version, Timestamp: " << 0 << ","
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
}

void BSPModel::Clock(Message& msg) {
  int updated_min_clock = progress_tracker_.AdvanceAndGetChangedMinClock(msg.meta.sender);
  int progress = progress_tracker_.GetProgress(msg.meta.sender);
  CHECK_LE(progress, progress_tracker_.GetMinClock() + 1);
  if (updated_min_clock != -1) {  // min clock updated
    for (auto add_req : add_buffer_) {
      storage_->Add(add_req);
    }
    add_buffer_.clear();

    storage_->FinishIter();

    for (auto get_req : get_buffer_) {
      reply_queue_->Push(storage_->Get(get_req));
    }
    get_buffer_.clear();

    if (dump_interval_ > 0 && updated_min_clock % dump_interval_ == 0) {
      LOG(INFO) << "[BSPModel] Version, Timestamp: " << updated_min_clock << ","
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
      Dump(server_id_);
    }
  }
}

void BSPModel::Add(Message& msg) {
  CHECK(progress_tracker_.CheckThreadValid(msg.meta.sender));
  int progress = progress_tracker_.GetProgress(msg.meta.sender);
  if (progress == progress_tracker_.GetMinClock()) {
    add_buffer_.push_back(msg);
  } else {
    CHECK(false) << "progress error in BSPModel::Add";
  }
}

void BSPModel::Get(Message& msg) {
  CHECK(progress_tracker_.CheckThreadValid(msg.meta.sender));
  int progress = progress_tracker_.GetProgress(msg.meta.sender);
  if (progress == progress_tracker_.GetMinClock() + 1) {
    get_buffer_.push_back(msg);
  } else if (progress == progress_tracker_.GetMinClock()) {
    reply_queue_->Push(storage_->Get(msg));
  } else {
    CHECK(false) << "progress error in BSPModel::Get { get progress: " << progress
                 << ", min clock: " << progress_tracker_.GetMinClock() << " }";
  }
}

int BSPModel::GetProgress(int tid) { return progress_tracker_.GetProgress(tid); }

int BSPModel::GetGetPendingSize() { return get_buffer_.size(); }

int BSPModel::GetAddPendingSize() { return add_buffer_.size(); }

void BSPModel::ResetWorker(Message& msg) {
  CHECK_EQ(msg.data.size(), 1);
  third_party::SArray<uint32_t> tids;
  tids = msg.data[0];
  std::vector<uint32_t> tids_vec;
  for (auto tid : tids)
    tids_vec.push_back(tid);
  this->progress_tracker_.Init(tids_vec);
  Message reply_msg;
  reply_msg.meta.model_id = model_id_;
  reply_msg.meta.recver = msg.meta.sender;
  reply_msg.meta.flag = Flag::kResetWorkerInModel;
  reply_queue_->Push(reply_msg);
}

void BSPModel::Dump(int server_id, const std::string& path) {
  storage_->WriteTo(path + "MODEL_v" + std::to_string(progress_tracker_.GetMinClock()) + "_part" +
                    std::to_string(server_id));
}

void BSPModel::Load(const std::string& file_name) { storage_->LoadFrom(file_name); }

}  // namespace flexps
