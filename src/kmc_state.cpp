#include "kmc_state.h"

#include <cfenv>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char* kCheckpointMagic = "TUTRAST_KMC_CHECKPOINT";

class BinaryWriter {
public:
    template <typename T>
    void write_scalar(const T& value) {
        const char* ptr = reinterpret_cast<const char*>(&value);
        buffer_.append(ptr, sizeof(T));
    }

    void write_string(const std::string& value) {
        write_size(value.size());
        buffer_.append(value.data(), value.size());
    }

    void write_bytes(const unsigned char* data, size_t size) {
        write_size(size);
        if (size > 0) {
            buffer_.append(reinterpret_cast<const char*>(data), size);
        }
    }

    template <typename T>
    void write_vector(const std::vector<T>& values) {
        write_size(values.size());
        for (const auto& value : values) {
            write_scalar(value);
        }
    }

    void write_logged_steps(const std::vector<LoggedStep>& steps) {
        write_size(steps.size());
        for (const auto& step : steps) {
            for (int particle_id : step.particle_ids) write_scalar(particle_id);
            for (int particle_sign : step.particle_signs) write_scalar(particle_sign);
            write_scalar(step.process_index);
        }
    }

    void write_axis_sums(const std::vector<AxisSums>& values) {
        write_size(values.size());
        for (const auto& value : values) {
            write_scalar(value.x);
            write_scalar(value.y);
            write_scalar(value.z);
        }
    }

    void write_positions(const std::vector<Position3D>& values) {
        write_size(values.size());
        for (const auto& value : values) {
            write_scalar(value.x);
            write_scalar(value.y);
            write_scalar(value.z);
        }
    }

    const std::string& buffer() const {
        return buffer_;
    }

private:
    void write_size(size_t value) {
        write_scalar(static_cast<uint64_t>(value));
    }

    std::string buffer_;
};

class BinaryReader {
public:
    explicit BinaryReader(const std::string& buffer)
        : buffer_(buffer), offset_(0) {}

    template <typename T>
    T read_scalar() {
        ensure_available(sizeof(T));
        T value;
        std::memcpy(&value, buffer_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return value;
    }

    std::string read_string() {
        const uint64_t size = read_scalar<uint64_t>();
        ensure_available(size);
        std::string value(buffer_.data() + offset_, buffer_.data() + offset_ + size);
        offset_ += static_cast<size_t>(size);
        return value;
    }

    std::vector<unsigned char> read_byte_vector() {
        const uint64_t size = read_scalar<uint64_t>();
        ensure_available(size);
        std::vector<unsigned char> values(size);
        if (size > 0) {
            std::memcpy(values.data(), buffer_.data() + offset_, static_cast<size_t>(size));
        }
        offset_ += static_cast<size_t>(size);
        return values;
    }

    template <typename T>
    std::vector<T> read_vector() {
        const uint64_t size = read_scalar<uint64_t>();
        std::vector<T> values;
        values.reserve(size);
        for (uint64_t i = 0; i < size; i++) {
            values.push_back(read_scalar<T>());
        }
        return values;
    }

    std::vector<LoggedStep> read_logged_steps() {
        const uint64_t size = read_scalar<uint64_t>();
        std::vector<LoggedStep> values(size);
        for (auto& step : values) {
            for (int& particle_id : step.particle_ids) particle_id = read_scalar<int>();
            for (int& particle_sign : step.particle_signs) particle_sign = read_scalar<int>();
            step.process_index = read_scalar<int>();
        }
        return values;
    }

    std::vector<AxisSums> read_axis_sums() {
        const uint64_t size = read_scalar<uint64_t>();
        std::vector<AxisSums> values(size);
        for (auto& value : values) {
            value.x = read_scalar<double>();
            value.y = read_scalar<double>();
            value.z = read_scalar<double>();
        }
        return values;
    }

    std::vector<Position3D> read_positions() {
        const uint64_t size = read_scalar<uint64_t>();
        std::vector<Position3D> values(size);
        for (auto& value : values) {
            value.x = read_scalar<double>();
            value.y = read_scalar<double>();
            value.z = read_scalar<double>();
        }
        return values;
    }

    void ensure_consumed() const {
        if (offset_ != buffer_.size()) {
            throw std::runtime_error("Checkpoint payload has trailing unread bytes");
        }
    }

    size_t offset() const {
        return offset_;
    }

private:
    void ensure_available(uint64_t size) const {
        if (size > buffer_.size() - offset_) {
            throw std::runtime_error("Checkpoint payload is truncated");
        }
    }

    const std::string& buffer_;
    size_t offset_;
};

uint64_t fnv1a_64(const std::string& bytes) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;
    for (unsigned char byte : bytes) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= kPrime;
    }
    return hash;
}

std::string serialize_payload(const KmcCheckpointData& checkpoint) {
    BinaryWriter writer;

    writer.write_string(checkpoint.header.build_fingerprint);
    writer.write_string(checkpoint.header.rng_engine);
    writer.write_string(checkpoint.header.run_label);
    writer.write_scalar(checkpoint.header.model_fingerprint);
    writer.write_scalar(checkpoint.header.checkpoint_every);
    writer.write_scalar(checkpoint.header.floating_rounding_mode);

    writer.write_scalar(checkpoint.state.current_step);
    writer.write_scalar(checkpoint.state.target_steps);
    writer.write_scalar(checkpoint.state.lag_plan_steps);
    writer.write_scalar(checkpoint.state.requested_particles);
    writer.write_scalar(checkpoint.state.effective_particles);
    writer.write_scalar(checkpoint.state.current_time);
    writer.write_scalar(checkpoint.state.available_lags);
    writer.write_vector(checkpoint.state.msd_steps);
    writer.write_bytes(checkpoint.state.types.data(), checkpoint.state.types.size());
    writer.write_vector(checkpoint.state.slot_to_particle_id);
    writer.write_vector(checkpoint.state.site_to_slot);
    writer.write_vector(checkpoint.state.time_history);
    writer.write_logged_steps(checkpoint.state.event_history);
    writer.write_vector(checkpoint.state.sum_dt);
    writer.write_axis_sums(checkpoint.state.current_squared_diffs);
    writer.write_axis_sums(checkpoint.state.accumulated_squared_diffs);
    writer.write_positions(checkpoint.state.lag_diffs);
    writer.write_string(checkpoint.state.rng_state);

    return writer.buffer();
}

KmcCheckpointData deserialize_payload(uint32_t schema_version, const std::string& payload) {
    BinaryReader reader(payload);
    KmcCheckpointData checkpoint;
    checkpoint.header.schema_version = schema_version;

    checkpoint.header.build_fingerprint = reader.read_string();
    checkpoint.header.rng_engine = reader.read_string();
    checkpoint.header.run_label = reader.read_string();
    checkpoint.header.model_fingerprint = reader.read_scalar<uint64_t>();
    checkpoint.header.checkpoint_every = reader.read_scalar<uint64_t>();
    checkpoint.header.floating_rounding_mode = reader.read_scalar<int>();

    checkpoint.state.current_step = reader.read_scalar<uint64_t>();
    checkpoint.state.target_steps = reader.read_scalar<uint64_t>();
    checkpoint.state.lag_plan_steps = reader.read_scalar<uint64_t>();
    checkpoint.state.requested_particles = reader.read_scalar<int>();
    checkpoint.state.effective_particles = reader.read_scalar<int>();
    checkpoint.state.current_time = reader.read_scalar<double>();
    checkpoint.state.available_lags = reader.read_scalar<uint64_t>();
    checkpoint.state.msd_steps = reader.read_vector<int>();
    checkpoint.state.types = reader.read_byte_vector();
    checkpoint.state.slot_to_particle_id = reader.read_vector<int>();
    checkpoint.state.site_to_slot = reader.read_vector<int>();
    checkpoint.state.time_history = reader.read_vector<double>();
    checkpoint.state.event_history = reader.read_logged_steps();
    checkpoint.state.sum_dt = reader.read_vector<double>();
    checkpoint.state.current_squared_diffs = reader.read_axis_sums();
    checkpoint.state.accumulated_squared_diffs = reader.read_axis_sums();
    checkpoint.state.lag_diffs = reader.read_positions();
    checkpoint.state.rng_state = reader.read_string();

    reader.ensure_consumed();
    return checkpoint;
}

void fsync_directory(const std::filesystem::path& path) {
    const std::filesystem::path dir = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        throw std::runtime_error("Failed to open checkpoint directory for fsync");
    }
    if (::fsync(dir_fd) != 0) {
        const int saved_errno = errno;
        ::close(dir_fd);
        throw std::runtime_error("Failed to fsync checkpoint directory: " + std::string(std::strerror(saved_errno)));
    }
    ::close(dir_fd);
}

} // namespace

std::string current_build_fingerprint() {
    return std::string("compiler=") + __VERSION__ + ";built=" + __DATE__ + " " + __TIME__;
}

std::string current_rng_engine_name() {
    return "std::mt19937";
}

int current_floating_rounding_mode() {
    return std::fegetround();
}

void write_kmc_checkpoint(const std::string& path, const KmcCheckpointData& checkpoint) {
    KmcCheckpointData copy = checkpoint;
    copy.header.schema_version = 1;
    const std::string payload = serialize_payload(copy);
    const uint64_t checksum = fnv1a_64(payload);

    BinaryWriter writer;
    writer.write_string(kCheckpointMagic);
    writer.write_scalar(copy.header.schema_version);
    writer.write_scalar(checksum);

    std::string final_bytes = writer.buffer();
    final_bytes.append(payload);

    const std::filesystem::path checkpoint_path(path);
    if (!checkpoint_path.parent_path().empty()) {
        std::filesystem::create_directories(checkpoint_path.parent_path());
    }
    const std::filesystem::path temp_path = checkpoint_path.string() + ".tmp";

    const int fd = ::open(temp_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to open temporary checkpoint file for write");
    }

    size_t total_written = 0;
    while (total_written < final_bytes.size()) {
        const ssize_t written = ::write(fd,
                                        final_bytes.data() + total_written,
                                        final_bytes.size() - total_written);
        if (written < 0) {
            const int saved_errno = errno;
            ::close(fd);
            throw std::runtime_error("Failed to write checkpoint: " + std::string(std::strerror(saved_errno)));
        }
        total_written += static_cast<size_t>(written);
    }

    if (::fsync(fd) != 0) {
        const int saved_errno = errno;
        ::close(fd);
        throw std::runtime_error("Failed to fsync checkpoint file: " + std::string(std::strerror(saved_errno)));
    }

    ::close(fd);

    if (::rename(temp_path.c_str(), checkpoint_path.c_str()) != 0) {
        const int saved_errno = errno;
        throw std::runtime_error("Failed to atomically replace checkpoint: " + std::string(std::strerror(saved_errno)));
    }

    fsync_directory(checkpoint_path);
}

KmcCheckpointData read_kmc_checkpoint(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open checkpoint file: " + path);
    }

    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    BinaryReader reader(bytes);

    const std::string magic = reader.read_string();
    if (magic != kCheckpointMagic) {
        throw std::runtime_error("Checkpoint magic mismatch");
    }

    const uint32_t schema_version = reader.read_scalar<uint32_t>();
    if (schema_version != 1) {
        throw std::runtime_error("Unsupported checkpoint schema version: " + std::to_string(schema_version));
    }

    const uint64_t stored_checksum = reader.read_scalar<uint64_t>();
    const std::string payload = bytes.substr(reader.offset());
    const uint64_t computed_checksum = fnv1a_64(payload);
    if (computed_checksum != stored_checksum) {
        throw std::runtime_error("Checkpoint checksum mismatch");
    }

    return deserialize_payload(schema_version, payload);
}

void validate_kmc_checkpoint_identity(const KmcCheckpointData& checkpoint,
                                      uint64_t expected_model_fingerprint,
                                      const std::string& expected_run_label) {
    if (checkpoint.header.build_fingerprint != current_build_fingerprint()) {
        throw std::runtime_error("Checkpoint build fingerprint does not match the current binary");
    }
    if (checkpoint.header.rng_engine != current_rng_engine_name()) {
        throw std::runtime_error("Checkpoint RNG engine does not match the current binary");
    }
    if (checkpoint.header.floating_rounding_mode != current_floating_rounding_mode()) {
        throw std::runtime_error("Checkpoint floating-point environment does not match the current process");
    }
    if (checkpoint.header.model_fingerprint != expected_model_fingerprint) {
        throw std::runtime_error("Checkpoint model fingerprint does not match the current derived KMC model");
    }
    if (checkpoint.header.run_label != expected_run_label) {
        throw std::runtime_error("Checkpoint run label does not match the current execution target");
    }
}
