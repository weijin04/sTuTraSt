#include "kmc_state.h"

#include <cfenv>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char* kCheckpointMagic = "TUTRAST_KMC_CHECKPOINT";
constexpr uint32_t kCheckpointSchemaVersion = 1;

class BinaryWriter {
public:
    void reserve(size_t size) {
        buffer_.reserve(size);
    }

    template <typename T>
    void write_scalar(const T& value) {
        const char* ptr = reinterpret_cast<const char*>(&value);
        buffer_.append(ptr, sizeof(T));
    }

    void write_string(std::string_view value) {
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
        static_assert(std::is_trivially_copyable_v<T>, "write_vector requires trivially copyable T");
        write_size(values.size());
        if (!values.empty()) {
            buffer_.append(reinterpret_cast<const char*>(values.data()),
                           values.size() * sizeof(T));
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

    std::string take_buffer() {
        return std::move(buffer_);
    }

private:
    void write_size(size_t value) {
        write_scalar(static_cast<uint64_t>(value));
    }

    std::string buffer_;
};

class BinaryReader {
public:
    explicit BinaryReader(std::string_view buffer, size_t offset = 0)
        : buffer_(buffer), offset_(offset) {}

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
        std::string value(buffer_.substr(offset_, static_cast<size_t>(size)));
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
        static_assert(std::is_trivially_copyable_v<T>, "read_vector requires trivially copyable T");
        const uint64_t size = read_scalar<uint64_t>();
        const size_t bytes = static_cast<size_t>(size) * sizeof(T);
        ensure_available(bytes);
        std::vector<T> values(static_cast<size_t>(size));
        if (bytes > 0) {
            std::memcpy(values.data(), buffer_.data() + offset_, bytes);
        }
        offset_ += bytes;
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

    std::string_view buffer_;
    size_t offset_;
};

size_t serialized_string_size(std::string_view value) {
    return sizeof(uint64_t) + value.size();
}

template <typename T>
size_t serialized_vector_size(const std::vector<T>& values) {
    return sizeof(uint64_t) + values.size() * sizeof(T);
}

size_t serialized_logged_steps_size(const std::vector<LoggedStep>& steps) {
    return sizeof(uint64_t) + steps.size() * (sizeof(int) * 5);
}

size_t serialized_axis_sums_size(const std::vector<AxisSums>& values) {
    return sizeof(uint64_t) + values.size() * (sizeof(double) * 3);
}

size_t serialized_positions_size(const std::vector<Position3D>& values) {
    return sizeof(uint64_t) + values.size() * (sizeof(double) * 3);
}

size_t estimate_payload_size(const KmcCheckpointHeader& header, const KmcRunState& state) {
    size_t size = 0;
    size += serialized_string_size(header.build_fingerprint);
    size += serialized_string_size(header.rng_engine);
    size += serialized_string_size(header.run_label);
    size += sizeof(header.model_fingerprint);
    size += sizeof(header.checkpoint_every);
    size += sizeof(header.floating_rounding_mode);

    size += sizeof(state.current_step);
    size += sizeof(state.target_steps);
    size += sizeof(state.lag_plan_steps);
    size += sizeof(state.requested_particles);
    size += sizeof(state.effective_particles);
    size += sizeof(state.current_time);
    size += sizeof(state.available_lags);
    size += serialized_vector_size(state.msd_steps);
    size += sizeof(uint64_t) + state.types.size();
    size += serialized_vector_size(state.slot_to_particle_id);
    size += serialized_vector_size(state.site_to_slot);
    size += serialized_vector_size(state.time_history);
    size += serialized_logged_steps_size(state.event_history);
    size += serialized_vector_size(state.sum_dt);
    size += serialized_axis_sums_size(state.current_squared_diffs);
    size += serialized_axis_sums_size(state.accumulated_squared_diffs);
    size += serialized_positions_size(state.lag_diffs);
    size += serialized_string_size(state.rng_state);
    return size;
}

uint64_t fnv1a_64(std::string_view bytes) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;
    for (unsigned char byte : bytes) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= kPrime;
    }
    return hash;
}

std::string serialize_payload(const KmcCheckpointHeader& header, const KmcRunState& state) {
    BinaryWriter writer;
    writer.reserve(estimate_payload_size(header, state));

    writer.write_string(header.build_fingerprint);
    writer.write_string(header.rng_engine);
    writer.write_string(header.run_label);
    writer.write_scalar(header.model_fingerprint);
    writer.write_scalar(header.checkpoint_every);
    writer.write_scalar(header.floating_rounding_mode);

    writer.write_scalar(state.current_step);
    writer.write_scalar(state.target_steps);
    writer.write_scalar(state.lag_plan_steps);
    writer.write_scalar(state.requested_particles);
    writer.write_scalar(state.effective_particles);
    writer.write_scalar(state.current_time);
    writer.write_scalar(state.available_lags);
    writer.write_vector(state.msd_steps);
    writer.write_bytes(state.types.data(), state.types.size());
    writer.write_vector(state.slot_to_particle_id);
    writer.write_vector(state.site_to_slot);
    writer.write_vector(state.time_history);
    writer.write_logged_steps(state.event_history);
    writer.write_vector(state.sum_dt);
    writer.write_axis_sums(state.current_squared_diffs);
    writer.write_axis_sums(state.accumulated_squared_diffs);
    writer.write_positions(state.lag_diffs);
    writer.write_string(state.rng_state);

    return writer.take_buffer();
}

KmcCheckpointData deserialize_payload(uint32_t schema_version, std::string_view payload) {
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

void write_all(int fd, std::string_view bytes) {
    size_t total_written = 0;
    while (total_written < bytes.size()) {
        const ssize_t written = ::write(fd,
                                        bytes.data() + total_written,
                                        bytes.size() - total_written);
        if (written < 0) {
            const int saved_errno = errno;
            throw std::runtime_error("Failed to write checkpoint: " + std::string(std::strerror(saved_errno)));
        }
        total_written += static_cast<size_t>(written);
    }
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
    write_kmc_checkpoint(path, checkpoint.header, checkpoint.state);
}

void write_kmc_checkpoint(const std::string& path,
                          const KmcCheckpointHeader& header,
                          const KmcRunState& state) {
    const std::string payload = serialize_payload(header, state);
    const uint64_t checksum = fnv1a_64(payload);

    BinaryWriter writer;
    writer.write_string(kCheckpointMagic);
    writer.write_scalar(kCheckpointSchemaVersion);
    writer.write_scalar(checksum);
    std::string prefix = writer.take_buffer();

    const std::filesystem::path checkpoint_path(path);
    if (!checkpoint_path.parent_path().empty()) {
        std::filesystem::create_directories(checkpoint_path.parent_path());
    }
    const std::filesystem::path temp_path = checkpoint_path.string() + ".tmp";

    const int fd = ::open(temp_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to open temporary checkpoint file for write");
    }

    try {
        write_all(fd, prefix);
        write_all(fd, payload);
    } catch (...) {
        ::close(fd);
        throw;
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
    BinaryReader reader{std::string_view(bytes)};

    const std::string magic = reader.read_string();
    if (magic != kCheckpointMagic) {
        throw std::runtime_error("Checkpoint magic mismatch");
    }

    const uint32_t schema_version = reader.read_scalar<uint32_t>();
    if (schema_version != kCheckpointSchemaVersion) {
        throw std::runtime_error("Unsupported checkpoint schema version: " + std::to_string(schema_version));
    }

    const uint64_t stored_checksum = reader.read_scalar<uint64_t>();
    const std::string_view payload(bytes.data() + reader.offset(), bytes.size() - reader.offset());
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
