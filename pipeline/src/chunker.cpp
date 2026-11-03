// Chunker and split_games (chunker.hpp).
#include "chunker.hpp"

#include <utility>

namespace chesscoach::pipeline {

namespace {

constexpr std::string_view game_start = "\n[Event ";

} // namespace

Chunker::Chunker(std::streambuf& source, ChunkKind kind, std::size_t batch_bytes)
    : source_(&source), kind_(kind), batch_bytes_(batch_bytes == 0 ? 1 : batch_bytes) {}

std::size_t Chunker::last_cut() const {
    if (kind_ == ChunkKind::lines) {
        const std::size_t newline = pending_.rfind('\n');
        return newline == std::string::npos ? std::string::npos : newline + 1;
    }
    const std::size_t start = pending_.rfind(game_start);
    return start == std::string::npos ? std::string::npos : start + 1;
}

std::string Chunker::next() {
    while (!exhausted_) {
        const std::size_t old_size = pending_.size();
        const std::size_t want =
            pending_.size() < batch_bytes_ ? batch_bytes_ - pending_.size() : batch_bytes_;
        pending_.resize(old_size + want);
        const std::streamsize got =
            source_->sgetn(pending_.data() + old_size, static_cast<std::streamsize>(want));
        pending_.resize(old_size + static_cast<std::size_t>(got));
        if (got == 0) {
            exhausted_ = true;
            break;
        }
        if (pending_.size() < batch_bytes_) {
            continue;
        }
        const std::size_t cut = last_cut();
        if (cut != std::string::npos) {
            std::string batch = pending_.substr(0, cut);
            pending_.erase(0, cut);
            return batch;
        }
    }
    return std::exchange(pending_, std::string{});
}

std::vector<std::string_view> split_games(std::string_view batch) {
    std::vector<std::string_view> games;
    std::size_t begin = 0;
    while (begin < batch.size()) {
        const std::size_t found = batch.find(game_start, begin);
        const std::size_t end = found == std::string_view::npos ? batch.size() : found + 1;
        games.push_back(batch.substr(begin, end - begin));
        begin = end;
    }
    return games;
}

} // namespace chesscoach::pipeline
