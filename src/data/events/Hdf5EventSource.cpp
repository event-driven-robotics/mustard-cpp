#include "mustard/data/events/TabularEventSource.h"
#include "mustard/data/events/EventImport.h"

#include <hdf5.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace mustard {
namespace {

std::recursive_mutex g_hdf5_mutex;

template <decltype(H5Fclose) Closer>
class H5Auto {
public:
    H5Auto() = default;
    explicit H5Auto(hid_t id) : id_(id) {}
    ~H5Auto() { if (id_ >= 0) Closer(id_); }
    H5Auto(const H5Auto&) = delete;
    H5Auto& operator=(const H5Auto&) = delete;
    H5Auto(H5Auto&& o) noexcept : id_(o.id_) { o.id_ = -1; }
    H5Auto& operator=(H5Auto&& o) noexcept {
        if (this != &o) {
            if (id_ >= 0) Closer(id_);
            id_ = o.id_;
            o.id_ = -1;
        }
        return *this;
    }
    hid_t get() const { return id_; }
    bool valid() const { return id_ >= 0; }
    hid_t release() { hid_t ret = id_; id_ = -1; return ret; }
    void reset(hid_t id = -1) {
        if (id_ >= 0) Closer(id_);
        id_ = id;
    }
private:
    hid_t id_{-1};
};

using H5FileHandle = H5Auto<H5Fclose>;
using H5GroupHandle = H5Auto<H5Gclose>;
using H5DatasetHandle = H5Auto<H5Dclose>;
using H5DataspaceHandle = H5Auto<H5Sclose>;
using H5DatatypeHandle = H5Auto<H5Tclose>;
using H5PropertyHandle = H5Auto<H5Pclose>;

void silenceHdf5Errors() {
    H5Eset_auto(H5E_DEFAULT, nullptr, nullptr);
}

H5FileHandle openHdf5File(const std::string& path) {
    silenceHdf5Errors();
    hid_t fid = H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    return H5FileHandle(fid);
}

H5FileHandle openHdf5Memory(const std::vector<uint8_t>& image) {
    silenceHdf5Errors();
    if (image.empty()) return H5FileHandle();
    H5PropertyHandle fapl(H5Pcreate(H5P_FILE_ACCESS));
    if (!fapl.valid()) return H5FileHandle();
    if (H5Pset_fapl_core(fapl.get(), 65536, 0) < 0) return H5FileHandle();
    if (H5Pset_file_image(fapl.get(), const_cast<uint8_t*>(image.data()), image.size()) < 0) return H5FileHandle();
    hid_t fid = H5Fopen("in_memory.h5", H5F_ACC_RDONLY, fapl.get());
    return H5FileHandle(fid);
}

struct ObjectTokenKey {
    unsigned long fileno{0};
    unsigned long addr{0};
    bool operator<(const ObjectTokenKey& o) const {
        if (fileno != o.fileno) return fileno < o.fileno;
        return addr < o.addr;
    }
};

ObjectTokenKey getObjectKey(hid_t loc_id, const char* name) {
    ObjectTokenKey key;
    H5O_info_t info;
#if H5_VERSION_GE(1, 12, 0)
    if (H5Oget_info_by_name3(loc_id, name, &info, H5O_INFO_BASIC, H5P_DEFAULT) >= 0) {
        key.fileno = info.fileno;
        key.addr = static_cast<unsigned long>(info.addr);
    }
#else
    if (H5Oget_info_by_name(loc_id, name, &info, H5P_DEFAULT) >= 0) {
        key.fileno = info.fileno;
        key.addr = static_cast<unsigned long>(info.addr);
    }
#endif
    return key;
}

std::string typeDescription(hid_t type_id) {
    H5T_class_t tclass = H5Tget_class(type_id);
    std::size_t size = H5Tget_size(type_id);
    if (tclass == H5T_INTEGER) {
        H5T_sign_t sign = H5Tget_sign(type_id);
        return (sign == H5T_SGN_NONE ? "uint" : "int") + std::to_string(size * 8);
    } else if (tclass == H5T_FLOAT) {
        return "float" + std::to_string(size * 8);
    } else if (tclass == H5T_STRING) {
        return "string";
    } else if (tclass == H5T_COMPOUND) {
        int nmem = H5Tget_nmembers(type_id);
        std::string desc = "compound (";
        for (int i = 0; i < nmem; ++i) {
            char* name = H5Tget_member_name(type_id, i);
            if (i > 0) desc += ", ";
            desc += name ? name : "field";
            if (name) H5free_memory(name);
        }
        desc += ")";
        return desc;
    }
    return "other";
}

bool isNumericClass(H5T_class_t tclass) {
    return tclass == H5T_INTEGER || tclass == H5T_FLOAT;
}

struct DatasetInfo {
    bool valid{false};
    bool is_group{false};
    std::string path;
    std::vector<uint64_t> shape;
    std::string datatype;
    bool selectable{false};
    std::string reason;

    // Detailed metadata for source instantiation
    enum class Kind { Numeric2D, Numeric1D, Compound1D, ColumnGroup, StringScalar, String1D };
    Kind kind{Kind::Numeric1D};
    std::vector<std::string> columns;
    uint64_t total_rows{0};
    std::vector<std::string> child_dataset_names;
};

DatasetInfo inspectDatasetOrGroup(hid_t loc_id, const std::string& path, std::set<ObjectTokenKey>& visited) {
    DatasetInfo info;
    info.path = path;

    ObjectTokenKey key = getObjectKey(loc_id, path.c_str());
    if (key.fileno != 0 || key.addr != 0) {
        if (visited.count(key)) {
            info.valid = false;
            info.reason = "Cycle detected";
            return info;
        }
        visited.insert(key);
    }

    H5O_info_t oinfo;
#if H5_VERSION_GE(1, 12, 0)
    if (H5Oget_info_by_name3(loc_id, path.c_str(), &oinfo, H5O_INFO_BASIC, H5P_DEFAULT) < 0) {
#else
    if (H5Oget_info_by_name(loc_id, path.c_str(), &oinfo, H5P_DEFAULT) < 0) {
#endif
        info.reason = "Unreadable link or object";
        return info;
    }

    if (oinfo.type == H5O_TYPE_GROUP) {
        info.is_group = true;
        info.valid = true;
        info.datatype = "group";

        H5GroupHandle gh(H5Gopen(loc_id, path.c_str(), H5P_DEFAULT));
        if (!gh.valid()) {
            info.selectable = false;
            info.reason = "Unreadable group";
            return info;
        }

        hsize_t num_obj = 0;
        H5Gget_num_objs(gh.get(), &num_obj);
        uint64_t group_rows = 0;
        bool first = true;
        bool valid_group = true;
        std::vector<std::string> children;

        for (hsize_t i = 0; i < num_obj; ++i) {
            char name_buf[256];
            if (H5Gget_objname_by_idx(gh.get(), i, name_buf, sizeof(name_buf)) <= 0) continue;
            int obj_type = H5Gget_objtype_by_idx(gh.get(), i);
            if (obj_type == H5G_DATASET) {
                H5DatasetHandle dh(H5Dopen(gh.get(), name_buf, H5P_DEFAULT));
                if (!dh.valid()) { valid_group = false; break; }
                H5DataspaceHandle sh(H5Dget_space(dh.get()));
                if (!sh.valid()) { valid_group = false; break; }
                int ndims = H5Sget_simple_extent_ndims(sh.get());
                if (ndims != 1) { valid_group = false; break; }
                hsize_t dim = 0;
                H5Sget_simple_extent_dims(sh.get(), &dim, nullptr);
                H5DatatypeHandle th(H5Dget_type(dh.get()));
                if (!th.valid() || !isNumericClass(H5Tget_class(th.get()))) { valid_group = false; break; }

                if (first) {
                    group_rows = dim;
                    first = false;
                } else if (dim != group_rows) {
                    valid_group = false;
                    break;
                }
                children.push_back(name_buf);
            }
        }

        if (valid_group && !children.empty() && group_rows > 0) {
            std::sort(children.begin(), children.end());
            info.selectable = true;
            info.kind = DatasetInfo::Kind::ColumnGroup;
            info.columns = children;
            info.child_dataset_names = children;
            info.total_rows = group_rows;
            info.shape = {group_rows, static_cast<uint64_t>(children.size())};
        } else {
            info.selectable = false;
            info.reason = "Group contains no equal-length numeric datasets";
        }
        return info;
    }

    if (oinfo.type == H5O_TYPE_DATASET) {
        info.is_group = false;
        info.valid = true;

        H5DatasetHandle dh(H5Dopen(loc_id, path.c_str(), H5P_DEFAULT));
        if (!dh.valid()) {
            info.selectable = false;
            info.reason = "Unreadable dataset";
            return info;
        }

        H5PropertyHandle plist(H5Dget_create_plist(dh.get()));
        if (plist.valid()) {
            int nfilters = H5Pget_nfilters(plist.get());
            for (int f = 0; f < nfilters; ++f) {
                unsigned flags = 0;
                std::size_t cd_nelmts = 0;
                char name[128];
                H5Z_filter_t filter_id = H5Pget_filter(plist.get(), f, &flags, &cd_nelmts, nullptr, sizeof(name), name, nullptr);
                if (!H5Zfilter_avail(filter_id)) {
                    info.selectable = false;
                    info.reason = "Dataset requires missing compression filter";
                    return info;
                }
            }
        }

        H5DataspaceHandle sh(H5Dget_space(dh.get()));
        if (!sh.valid()) {
            info.selectable = false;
            info.reason = "Unreadable dataspace";
            return info;
        }

        int ndims = H5Sget_simple_extent_ndims(sh.get());
        std::vector<hsize_t> dims(ndims);
        if (ndims > 0) H5Sget_simple_extent_dims(sh.get(), dims.data(), nullptr);

        for (auto d : dims) info.shape.push_back(d);

        H5DatatypeHandle th(H5Dget_type(dh.get()));
        if (!th.valid()) {
            info.selectable = false;
            info.reason = "Unreadable datatype";
            return info;
        }

        info.datatype = typeDescription(th.get());
        H5T_class_t tclass = H5Tget_class(th.get());

        if (ndims == 0 && tclass == H5T_STRING) {
            info.selectable = true;
            info.kind = DatasetInfo::Kind::StringScalar;
            info.total_rows = 1;
            return info;
        }

        if (ndims == 1 && tclass == H5T_STRING) {
            info.selectable = true;
            info.kind = DatasetInfo::Kind::String1D;
            info.total_rows = dims[0];
            return info;
        }

        if (ndims == 1 && tclass == H5T_COMPOUND) {
            info.selectable = dims[0] > 0;
            if (info.selectable) {
                info.kind = DatasetInfo::Kind::Compound1D;
                info.total_rows = dims[0];
                int nmem = H5Tget_nmembers(th.get());
                for (int m = 0; m < nmem; ++m) {
                    char* name = H5Tget_member_name(th.get(), m);
                    info.columns.push_back(name ? name : "field" + std::to_string(m));
                    if (name) H5free_memory(name);
                }
            } else {
                info.reason = "Dataset is empty";
            }
            return info;
        }

        if (ndims == 1 && isNumericClass(tclass)) {
            info.selectable = dims[0] > 0;
            if (info.selectable) {
                info.kind = DatasetInfo::Kind::Numeric1D;
                info.total_rows = dims[0];
                info.columns = {"Column 1"};
            } else {
                info.reason = "Dataset is empty";
            }
            return info;
        }

        if (ndims == 2 && isNumericClass(tclass)) {
            info.selectable = dims[0] > 0 && dims[1] > 0;
            if (info.selectable) {
                info.kind = DatasetInfo::Kind::Numeric2D;
                info.total_rows = dims[0];
                for (hsize_t c = 0; c < dims[1]; ++c) {
                    info.columns.push_back("Column " + std::to_string(c + 1));
                }
            } else {
                info.reason = "Dataset is empty";
            }
            return info;
        }

        info.selectable = false;
        info.reason = "Unsupported layout or data type";
        return info;
    }

    info.reason = "Unsupported object type";
    return info;
}

void traverseHdf5(hid_t loc_id, const std::string& current_path, std::vector<Hdf5Entry>& entries, std::set<ObjectTokenKey>& visited) {
    DatasetInfo dinfo = inspectDatasetOrGroup(loc_id, current_path, visited);
    if (!dinfo.valid && current_path != "/") return;

    if (current_path != "/") {
        Hdf5Entry entry;
        entry.path = current_path;
        entry.group = dinfo.is_group;
        entry.shape = dinfo.shape;
        entry.datatype = dinfo.datatype;
        entry.selectable = dinfo.selectable;
        entry.reason = dinfo.reason;
        entries.push_back(entry);
    }

    if (dinfo.is_group) {
        H5GroupHandle gh(H5Gopen(loc_id, current_path.c_str(), H5P_DEFAULT));
        if (!gh.valid()) return;
        hsize_t num_obj = 0;
        H5Gget_num_objs(gh.get(), &num_obj);
        for (hsize_t i = 0; i < num_obj; ++i) {
            char name_buf[256];
            if (H5Gget_objname_by_idx(gh.get(), i, name_buf, sizeof(name_buf)) <= 0) continue;
            std::string child_path = current_path == "/" ? std::string("/") + name_buf : current_path + "/" + name_buf;
            traverseHdf5(loc_id, child_path, entries, visited);
        }
    }
}

class Hdf5EventSource final : public TabularEventSource {
public:
    Hdf5EventSource(H5FileHandle file, DatasetInfo info, std::string path,
                    const EventImportOptions& options, ImportCancellation cancel)
        : file_(std::move(file)), info_(std::move(info)), path_(std::move(path)),
          options_(options), cancel_(std::move(cancel)) {
        configureDirectEvents();
    }

    const std::vector<std::string>& columns() const override { return info_.columns; }
    uint64_t checkpoint() const override { return current_row_; }

    bool seek(uint64_t checkpoint, ImportDiagnostic& error) override {
        if (cancelled(error)) return false;
        if (checkpoint > info_.total_rows) {
            error = {path_, options_.dataset, 0, {}, "Invalid HDF5 checkpoint", false};
            return false;
        }
        current_row_ = checkpoint;
        return true;
    }

    bool readRows(std::size_t max_rows, std::vector<TableRow>& rows, ImportDiagnostic& error) override {
        rows.clear();
        if (cancelled(error)) return false;
        if (current_row_ >= info_.total_rows) return true;

        std::size_t count = static_cast<std::size_t>(
            std::min<uint64_t>(max_rows, info_.total_rows - current_row_));
        if (count == 0) return true;

        std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);

        if (info_.kind == DatasetInfo::Kind::Numeric2D || info_.kind == DatasetInfo::Kind::Numeric1D) {
            H5DatasetHandle dh(H5Dopen(file_.get(), options_.dataset.c_str(), H5P_DEFAULT));
            if (!dh.valid()) return fail(error, "Cannot open HDF5 dataset");
            H5DataspaceHandle file_space(H5Dget_space(dh.get()));
            if (!file_space.valid()) return fail(error, "Cannot get HDF5 dataspace");

            std::size_t num_cols = info_.columns.size();
            hsize_t offset[2] = {current_row_, 0};
            hsize_t slab_count[2] = {count, num_cols};
            if (info_.kind == DatasetInfo::Kind::Numeric1D) {
                slab_count[1] = 1;
            }

            if (H5Sselect_hyperslab(file_space.get(), H5S_SELECT_SET, offset, nullptr,
                                    info_.kind == DatasetInfo::Kind::Numeric2D ? slab_count : &slab_count[0], nullptr) < 0) {
                return fail(error, "Hyperslab selection failed");
            }

            H5DataspaceHandle mem_space(H5Screate_simple(info_.kind == DatasetInfo::Kind::Numeric2D ? 2 : 1, slab_count, nullptr));
            H5DatatypeHandle th(H5Dget_type(dh.get()));
            H5T_class_t tclass = H5Tget_class(th.get());

            if (tclass == H5T_FLOAT) {
                std::vector<double> buffer(count * num_cols);
                if (H5Dread(dh.get(), H5T_NATIVE_DOUBLE, mem_space.get(), file_space.get(), H5P_DEFAULT, buffer.data()) < 0) {
                    return fail(error, "Failed to read HDF5 double matrix");
                }
                rows.resize(count);
                for (std::size_t r = 0; r < count; ++r) {
                    rows[r].resize(num_cols);
                    for (std::size_t c = 0; c < num_cols; ++c) {
                        rows[r][c] = buffer[r * num_cols + c];
                    }
                }
            } else {
                H5T_sign_t sign = H5Tget_sign(th.get());
                if (sign == H5T_SGN_NONE) {
                    std::vector<uint64_t> buffer(count * num_cols);
                    if (H5Dread(dh.get(), H5T_NATIVE_UINT64, mem_space.get(), file_space.get(), H5P_DEFAULT, buffer.data()) < 0) {
                        return fail(error, "Failed to read HDF5 uint matrix");
                    }
                    rows.resize(count);
                    for (std::size_t r = 0; r < count; ++r) {
                        rows[r].resize(num_cols);
                        for (std::size_t c = 0; c < num_cols; ++c) {
                            rows[r][c] = buffer[r * num_cols + c];
                        }
                    }
                } else {
                    std::vector<int64_t> buffer(count * num_cols);
                    if (H5Dread(dh.get(), H5T_NATIVE_INT64, mem_space.get(), file_space.get(), H5P_DEFAULT, buffer.data()) < 0) {
                        return fail(error, "Failed to read HDF5 int matrix");
                    }
                    rows.resize(count);
                    for (std::size_t r = 0; r < count; ++r) {
                        rows[r].resize(num_cols);
                        for (std::size_t c = 0; c < num_cols; ++c) {
                            rows[r][c] = buffer[r * num_cols + c];
                        }
                    }
                }
            }
            current_row_ += count;
            return true;
        }

        if (info_.kind == DatasetInfo::Kind::ColumnGroup) {
            std::size_t num_cols = info_.child_dataset_names.size();
            rows.resize(count);
            for (std::size_t r = 0; r < count; ++r) rows[r].resize(num_cols);

            H5GroupHandle gh(H5Gopen(file_.get(), options_.dataset.c_str(), H5P_DEFAULT));
            if (!gh.valid()) return fail(error, "Cannot open HDF5 column group");

            for (std::size_t c = 0; c < num_cols; ++c) {
                H5DatasetHandle dh(H5Dopen(gh.get(), info_.child_dataset_names[c].c_str(), H5P_DEFAULT));
                if (!dh.valid()) return fail(error, "Cannot open column dataset " + info_.child_dataset_names[c]);
                H5DataspaceHandle file_space(H5Dget_space(dh.get()));
                hsize_t offset = current_row_;
                hsize_t slab_count = count;
                H5Sselect_hyperslab(file_space.get(), H5S_SELECT_SET, &offset, nullptr, &slab_count, nullptr);
                H5DataspaceHandle mem_space(H5Screate_simple(1, &slab_count, nullptr));
                H5DatatypeHandle th(H5Dget_type(dh.get()));
                H5T_class_t tclass = H5Tget_class(th.get());

                if (tclass == H5T_FLOAT) {
                    std::vector<double> buffer(count);
                    H5Dread(dh.get(), H5T_NATIVE_DOUBLE, mem_space.get(), file_space.get(), H5P_DEFAULT, buffer.data());
                    for (std::size_t r = 0; r < count; ++r) rows[r][c] = buffer[r];
                } else if (H5Tget_sign(th.get()) == H5T_SGN_NONE) {
                    std::vector<uint64_t> buffer(count);
                    H5Dread(dh.get(), H5T_NATIVE_UINT64, mem_space.get(), file_space.get(), H5P_DEFAULT, buffer.data());
                    for (std::size_t r = 0; r < count; ++r) rows[r][c] = buffer[r];
                } else {
                    std::vector<int64_t> buffer(count);
                    H5Dread(dh.get(), H5T_NATIVE_INT64, mem_space.get(), file_space.get(), H5P_DEFAULT, buffer.data());
                    for (std::size_t r = 0; r < count; ++r) rows[r][c] = buffer[r];
                }
            }
            current_row_ += count;
            return true;
        }

        if (info_.kind == DatasetInfo::Kind::Compound1D) {
            H5DatasetHandle dh(H5Dopen(file_.get(), options_.dataset.c_str(), H5P_DEFAULT));
            if (!dh.valid()) return fail(error, "Cannot open compound dataset");
            H5DataspaceHandle file_space(H5Dget_space(dh.get()));
            hsize_t offset = current_row_;
            hsize_t slab_count = count;
            H5Sselect_hyperslab(file_space.get(), H5S_SELECT_SET, &offset, nullptr, &slab_count, nullptr);
            H5DataspaceHandle mem_space(H5Screate_simple(1, &slab_count, nullptr));

            H5DatatypeHandle file_type(H5Dget_type(dh.get()));
            int nmem = H5Tget_nmembers(file_type.get());
            rows.resize(count);

            for (int m = 0; m < nmem; ++m) {
                H5DatatypeHandle mem_type;
                H5T_class_t mclass = H5Tget_member_class(file_type.get(), m);
                std::vector<double> buf_double;
                std::vector<int64_t> buf_int64;
                std::vector<uint64_t> buf_uint64;

                hid_t compound_mem_type = H5Tcreate(H5T_COMPOUND, mclass == H5T_FLOAT ? sizeof(double) : sizeof(int64_t));
                char* mname = H5Tget_member_name(file_type.get(), m);
                hid_t field_native = mclass == H5T_FLOAT ? H5T_NATIVE_DOUBLE :
                                     (mclass == H5T_INTEGER && H5Tget_sign(file_type.get()) == H5T_SGN_NONE ? H5T_NATIVE_UINT64 : H5T_NATIVE_INT64);

                H5Tinsert(compound_mem_type, mname, 0, field_native);

                if (mclass == H5T_FLOAT) {
                    buf_double.resize(count);
                    H5Dread(dh.get(), compound_mem_type, mem_space.get(), file_space.get(), H5P_DEFAULT, buf_double.data());
                    for (std::size_t r = 0; r < count; ++r) rows[r].push_back(buf_double[r]);
                } else {
                    buf_int64.resize(count);
                    H5Dread(dh.get(), compound_mem_type, mem_space.get(), file_space.get(), H5P_DEFAULT, buf_int64.data());
                    for (std::size_t r = 0; r < count; ++r) rows[r].push_back(buf_int64[r]);
                }
                H5Tclose(compound_mem_type);
                if (mname) H5free_memory(mname);
            }
            current_row_ += count;
            return true;
        }

        return fail(error, "Unsupported HDF5 dataset read mode");
    }

    float progress() const override {
        return info_.total_rows ? static_cast<float>(current_row_) / static_cast<float>(info_.total_rows) : 1.f;
    }

    bool readsEventsDirectly() const noexcept override { return direct_events_; }

    bool readEvents(std::size_t max_rows, std::vector<DVSEvent>& events,
                    ImportDiagnostic& error) override {
        events.clear();
        if (!direct_events_) return fail(error, "Direct event reads are unavailable for this layout");
        if (cancelled(error)) return false;
        if (current_row_ >= info_.total_rows) return true;
        const std::size_t count = static_cast<std::size_t>(
            std::min<uint64_t>(max_rows, info_.total_rows - current_row_));
        if (count == 0) return true;

        std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
        H5GroupHandle group(H5Gopen(file_.get(), options_.dataset.c_str(), H5P_DEFAULT));
        if (!group.valid()) return fail(error, "Cannot open HDF5 column group");

        auto readColumn = [&](const std::string& name, hid_t native_type,
                              void* destination) -> bool {
            H5DatasetHandle dataset(H5Dopen(group.get(), name.c_str(), H5P_DEFAULT));
            if (!dataset.valid()) return false;
            H5DataspaceHandle file_space(H5Dget_space(dataset.get()));
            if (!file_space.valid()) return false;
            const hsize_t offset = current_row_;
            const hsize_t slab_count = count;
            if (H5Sselect_hyperslab(file_space.get(), H5S_SELECT_SET, &offset, nullptr,
                                    &slab_count, nullptr) < 0) return false;
            H5DataspaceHandle memory_space(H5Screate_simple(1, &slab_count, nullptr));
            return memory_space.valid() &&
                   H5Dread(dataset.get(), native_type, memory_space.get(), file_space.get(),
                           H5P_DEFAULT, destination) >= 0;
        };

        std::vector<uint16_t> xs(count), ys(count);
        std::vector<int64_t> signed_ts;
        std::vector<uint64_t> unsigned_ts;
        std::vector<int8_t> signed_p;
        std::vector<uint8_t> unsigned_p;
        if (!readColumn(direct_columns_[0], H5T_NATIVE_UINT16, xs.data()) ||
            !readColumn(direct_columns_[1], H5T_NATIVE_UINT16, ys.data()))
            return fail(error, "Failed to read HDF5 coordinate columns");
        if (timestamp_unsigned_) {
            unsigned_ts.resize(count);
            if (!readColumn(direct_columns_[2], H5T_NATIVE_UINT64, unsigned_ts.data()))
                return fail(error, "Failed to read HDF5 timestamp column");
        } else {
            signed_ts.resize(count);
            if (!readColumn(direct_columns_[2], H5T_NATIVE_INT64, signed_ts.data()))
                return fail(error, "Failed to read HDF5 timestamp column");
        }
        if (polarity_unsigned_) {
            unsigned_p.resize(count);
            if (!readColumn(direct_columns_[3], H5T_NATIVE_UINT8, unsigned_p.data()))
                return fail(error, "Failed to read HDF5 polarity column");
        } else {
            signed_p.resize(count);
            if (!readColumn(direct_columns_[3], H5T_NATIVE_INT8, signed_p.data()))
                return fail(error, "Failed to read HDF5 polarity column");
        }

        constexpr uint64_t max_time = static_cast<uint64_t>(
            std::numeric_limits<int64_t>::max() - 10'000);
        events.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            uint64_t timestamp = 0;
            if (timestamp_unsigned_) timestamp = unsigned_ts[i];
            else {
                if (signed_ts[i] < 0) return failAt(error, i, "timestamp",
                    "Timestamp must be nonnegative");
                timestamp = static_cast<uint64_t>(signed_ts[i]);
            }
            if (timestamp > max_time) return failAt(error, i, "timestamp",
                "Timestamp exceeds the microsecond playback range");
            const int polarity = polarity_unsigned_ ? static_cast<int>(unsigned_p[i])
                                                    : static_cast<int>(signed_p[i]);
            if (polarity != -1 && polarity != 0 && polarity != 1)
                return failAt(error, i, "polarity", "Polarity must be 0, 1, or -1");
            events.push_back({static_cast<int64_t>(timestamp), xs[i], ys[i], polarity == 1});
        }
        current_row_ += count;
        return true;
    }

    void finishImport() override { cancel_.reset(); }

private:
    void configureDirectEvents() {
        if (info_.kind != DatasetInfo::Kind::ColumnGroup ||
            options_.timestamp_unit != TimestampUnit::Microseconds) return;
        std::vector<bool> used(info_.child_dataset_names.size(), false);
        for (int field = 0; field < 4; ++field) {
            const int column = options_.columns[static_cast<std::size_t>(field)];
            if (column < 0 || static_cast<std::size_t>(column) >= info_.child_dataset_names.size() ||
                used[static_cast<std::size_t>(column)]) return;
            used[static_cast<std::size_t>(column)] = true;
            direct_columns_[static_cast<std::size_t>(field)] =
                info_.child_dataset_names[static_cast<std::size_t>(column)];
        }
        std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
        H5GroupHandle group(H5Gopen(file_.get(), options_.dataset.c_str(), H5P_DEFAULT));
        if (!group.valid()) return;
        for (int field = 0; field < 4; ++field) {
            H5DatasetHandle dataset(H5Dopen(group.get(), direct_columns_[field].c_str(), H5P_DEFAULT));
            H5DatatypeHandle type(dataset.valid() ? H5Dget_type(dataset.get()) : -1);
            if (!type.valid() || H5Tget_class(type.get()) != H5T_INTEGER) return;
            const auto bytes = H5Tget_size(type.get());
            const bool is_unsigned = H5Tget_sign(type.get()) == H5T_SGN_NONE;
            if ((field < 2 && (!is_unsigned || bytes > sizeof(uint16_t))) ||
                (field == 2 && bytes > sizeof(uint64_t)) ||
                (field == 3 && bytes > sizeof(uint8_t))) return;
            if (field == 2) timestamp_unsigned_ = is_unsigned;
            if (field == 3) polarity_unsigned_ = is_unsigned;
        }
        direct_events_ = true;
    }

    bool failAt(ImportDiagnostic& error, std::size_t offset,
                const std::string& field, const std::string& message) {
        error = {path_, options_.dataset, current_row_ + offset + 1, field, message, false};
        return false;
    }

    bool fail(ImportDiagnostic& error, const std::string& msg) {
        error = {path_, options_.dataset, current_row_ + 1, {}, msg, false};
        return false;
    }
    bool cancelled(ImportDiagnostic& error) {
        if (!importCancelled(cancel_)) return false;
        error = {path_, options_.dataset, current_row_ + 1, {}, "Import cancelled", true};
        return true;
    }

    H5FileHandle file_;
    DatasetInfo info_;
    std::string path_;
    EventImportOptions options_;
    ImportCancellation cancel_;
    uint64_t current_row_{0};
    bool direct_events_{false};
    bool timestamp_unsigned_{false};
    bool polarity_unsigned_{false};
    std::array<std::string, 4> direct_columns_;
};

std::unique_ptr<TabularEventSource> makeHdf5SourceFromInfo(
    H5FileHandle file, DatasetInfo info, const std::string& path,
    const EventImportOptions& options, ImportDiagnostic& error, ImportCancellation cancel) {
    if (!info.selectable) {
        error = {path, options.dataset, 0, {}, info.reason.empty() ? "Dataset is not selectable" : info.reason, false};
        return {};
    }

    if (info.kind == DatasetInfo::Kind::StringScalar || info.kind == DatasetInfo::Kind::String1D) {
        std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
        H5DatasetHandle dh(H5Dopen(file.get(), options.dataset.c_str(), H5P_DEFAULT));
        if (!dh.valid()) {
            error = {path, options.dataset, 0, {}, "Cannot open string dataset", false};
            return {};
        }
        std::string text;
        if (info.kind == DatasetInfo::Kind::StringScalar) {
            H5DatatypeHandle th(H5Dget_type(dh.get()));
            std::size_t size = H5Tget_size(th.get());
            if (H5Tis_variable_str(th.get())) {
                char* ptr = nullptr;
                H5Dread(dh.get(), th.get(), H5S_ALL, H5S_ALL, H5P_DEFAULT, &ptr);
                if (ptr) { text = ptr; H5free_memory(ptr); }
            } else {
                text.resize(size);
                H5Dread(dh.get(), th.get(), H5S_ALL, H5S_ALL, H5P_DEFAULT, text.data());
            }
        } else {
            hsize_t total = info.total_rows;
            std::ostringstream ss;
            H5DatatypeHandle th(H5Dget_type(dh.get()));
            if (H5Tis_variable_str(th.get())) {
                std::vector<char*> ptrs(total);
                H5Dread(dh.get(), th.get(), H5S_ALL, H5S_ALL, H5P_DEFAULT, ptrs.data());
                for (hsize_t i = 0; i < total; ++i) {
                    if (ptrs[i]) { ss << ptrs[i] << "\n"; H5free_memory(ptrs[i]); }
                }
            } else {
                std::size_t len = H5Tget_size(th.get());
                std::vector<char> buf(total * len);
                H5Dread(dh.get(), th.get(), H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
                for (hsize_t i = 0; i < total; ++i) {
                    std::string line(buf.data() + i * len, len);
                    ss << line << "\n";
                }
            }
            text = ss.str();
        }
        return createCsvSource(std::make_unique<std::istringstream>(text), options, error, std::move(cancel));
    }

    return std::make_unique<Hdf5EventSource>(std::move(file), std::move(info), path, options, std::move(cancel));
}

} // namespace

bool browseHdf5(const std::string& path, std::vector<Hdf5Entry>& entries,
                ImportDiagnostic& error, ImportCancellation cancel) {
    entries.clear();
    error = {};
    error.path = path;
    if (importCancelled(cancel)) { error.cancelled = true; return false; }

    std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
    H5FileHandle file = openHdf5File(path);
    if (!file.valid()) {
        error.message = "Cannot open HDF5 file";
        return false;
    }

    std::set<ObjectTokenKey> visited;
    traverseHdf5(file.get(), "/", entries, visited);
    return true;
}

bool browseHdf5Image(const std::vector<uint8_t>& image, std::vector<Hdf5Entry>& entries,
                     ImportDiagnostic& error, ImportCancellation cancel) {
    entries.clear();
    error = {};
    error.path = "<memory>";
    if (importCancelled(cancel)) { error.cancelled = true; return false; }

    std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
    H5FileHandle file = openHdf5Memory(image);
    if (!file.valid()) {
        error.message = "Cannot open in-memory HDF5 file";
        return false;
    }

    std::set<ObjectTokenKey> visited;
    traverseHdf5(file.get(), "/", entries, visited);
    return true;
}

std::unique_ptr<TabularEventSource> createHdf5Source(
    const std::string& path, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel) {
    error = {};
    error.path = path;
    error.dataset = options.dataset;

    std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
    H5FileHandle file = openHdf5File(path);
    if (!file.valid()) {
        error.message = "Cannot open HDF5 file";
        return {};
    }

    std::string dataset_path = options.dataset;
    if (dataset_path.empty()) {
        std::vector<Hdf5Entry> entries;
        std::set<ObjectTokenKey> visited;
        traverseHdf5(file.get(), "/", entries, visited);
        for (const auto& e : entries) {
            if (e.selectable) { dataset_path = e.path; break; }
        }
        if (dataset_path.empty()) {
            error.message = "No dataset was selected in the HDF5 file";
            return {};
        }
    }

    std::set<ObjectTokenKey> visited;
    DatasetInfo info = inspectDatasetOrGroup(file.get(), dataset_path, visited);
    return makeHdf5SourceFromInfo(std::move(file), std::move(info), path, options, error, std::move(cancel));
}

std::unique_ptr<TabularEventSource> createHdf5SourceFromImage(
    const std::vector<uint8_t>& image, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel) {
    error = {};
    error.path = "<memory>";
    error.dataset = options.dataset;

    std::lock_guard<std::recursive_mutex> lock(g_hdf5_mutex);
    H5FileHandle file = openHdf5Memory(image);
    if (!file.valid()) {
        error.message = "Cannot open in-memory HDF5 file";
        return {};
    }

    std::string dataset_path = options.dataset;
    if (dataset_path.empty()) {
        std::vector<Hdf5Entry> entries;
        std::set<ObjectTokenKey> visited;
        traverseHdf5(file.get(), "/", entries, visited);
        for (const auto& e : entries) {
            if (e.selectable) { dataset_path = e.path; break; }
        }
        if (dataset_path.empty()) {
            error.message = "No dataset was selected in the HDF5 file";
            return {};
        }
    }

    std::set<ObjectTokenKey> visited;
    DatasetInfo info = inspectDatasetOrGroup(file.get(), dataset_path, visited);
    return makeHdf5SourceFromInfo(std::move(file), std::move(info), "<memory>", options, error, std::move(cancel));
}

} // namespace mustard
