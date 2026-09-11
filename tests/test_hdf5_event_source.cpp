#include "mustard/data/events/TabularEventSource.h"
#include "mustard/data/events/EventImport.h"

#include <gtest/gtest.h>
#include <hdf5.h>

#include <memory>
#include <string>
#include <vector>

using namespace mustard;

namespace {

class Hdf5MemoryFile {
public:
    Hdf5MemoryFile() {
        hid_t fapl = H5Pcreate(H5P_FILE_ACCESS);
        H5Pset_fapl_core(fapl, 65536, 0);
        file_id_ = H5Fcreate("test_mem.h5", H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
        H5Pclose(fapl);
    }

    ~Hdf5MemoryFile() {
        if (file_id_ >= 0) H5Fclose(file_id_);
    }

    hid_t id() const { return file_id_; }

    std::vector<uint8_t> getImage() {
        H5Fflush(file_id_, H5F_SCOPE_GLOBAL);
        hid_t fapl = H5Fget_access_plist(file_id_);
        ssize_t size = H5Fget_file_image(file_id_, nullptr, 0);
        std::vector<uint8_t> buf(size);
        H5Fget_file_image(file_id_, buf.data(), size);
        H5Pclose(fapl);
        return buf;
    }

private:
    hid_t file_id_{-1};
};

std::string cellText(const TableRow& row, std::size_t index) {
    return tableCellText(row.at(index));
}

} // namespace

TEST(Hdf5EventSource, BrowsesHierarchyAndIdentifiesLayouts) {
    Hdf5MemoryFile hfile;
    hid_t fid = hfile.id();
    ASSERT_GE(fid, 0);

    // Create group /events
    hid_t gid = H5Gcreate2(fid, "/events", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    // Add 2D matrix dataset /events/matrix [3, 4]
    hsize_t dims2d[2] = {3, 4};
    hid_t sid2d = H5Screate_simple(2, dims2d, nullptr);
    hid_t did2d = H5Dcreate2(gid, "matrix", H5T_NATIVE_INT32, sid2d, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    int mat_data[3][4] = {
        {10, 20, 100, 1},
        {11, 21, 200, 0},
        {12, 22, 300, 1}
    };
    H5Dwrite(did2d, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, mat_data);
    H5Dclose(did2d);
    H5Sclose(sid2d);

    H5Gclose(gid);

    auto image = hfile.getImage();
    ImportDiagnostic error;
    std::vector<Hdf5Entry> entries;
    ASSERT_TRUE(browseHdf5Image(image, entries, error)) << error.message;

    bool found_matrix = false;
    for (const auto& entry : entries) {
        if (entry.path == "/events/matrix") {
            found_matrix = true;
            EXPECT_TRUE(entry.selectable);
            EXPECT_EQ(entry.shape, (std::vector<uint64_t>{3, 4}));
        }
    }
    EXPECT_TRUE(found_matrix);
}

TEST(Hdf5EventSource, Reads2DNumericMatrix) {
    Hdf5MemoryFile hfile;
    hid_t fid = hfile.id();

    hsize_t dims2d[2] = {2, 4};
    hid_t sid2d = H5Screate_simple(2, dims2d, nullptr);
    hid_t did2d = H5Dcreate2(fid, "events", H5T_NATIVE_INT64, sid2d, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    int64_t mat_data[2][4] = {
        {1, 2, 1000, 1},
        {3, 4, 2000, 0}
    };
    H5Dwrite(did2d, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, mat_data);
    H5Dclose(did2d);
    H5Sclose(sid2d);

    auto image = hfile.getImage();
    ImportDiagnostic error;
    EventImportOptions options;
    options.format = EventSourceFormat::Hdf5;
    options.dataset = "/events";

    auto source = createHdf5SourceFromImage(image, options, error);
    ASSERT_NE(source, nullptr) << error.message;

    EXPECT_EQ(source->columns().size(), 4u);
    std::vector<TableRow> rows;
    ASSERT_TRUE(source->readRows(10, rows, error)) << error.message;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(cellText(rows[0], 0), "1");
    EXPECT_EQ(cellText(rows[0], 2), "1000");
    EXPECT_EQ(cellText(rows[1], 0), "3");
    EXPECT_EQ(cellText(rows[1], 2), "2000");
}

TEST(Hdf5EventSource, ReadsColumnGroup) {
    Hdf5MemoryFile hfile;
    hid_t fid = hfile.id();

    hid_t gid = H5Gcreate2(fid, "/dvs", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    hsize_t dim[1] = {2};

    auto create_col = [&](const char* name, const uint32_t* data) {
        hid_t sid = H5Screate_simple(1, dim, nullptr);
        hid_t did = H5Dcreate2(gid, name, H5T_NATIVE_UINT32, sid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(did, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
        H5Dclose(did);
        H5Sclose(sid);
    };

    uint32_t x_data[2] = {5, 6};
    uint32_t y_data[2] = {7, 8};
    uint32_t ts_data[2] = {100, 200};
    uint32_t p_data[2] = {1, 0};

    create_col("x", x_data);
    create_col("y", y_data);
    create_col("ts", ts_data);
    create_col("p", p_data);
    H5Gclose(gid);

    auto image = hfile.getImage();
    ImportDiagnostic error;
    EventImportOptions options;
    options.format = EventSourceFormat::Hdf5;
    options.dataset = "/dvs";

    auto source = createHdf5SourceFromImage(image, options, error);
    ASSERT_NE(source, nullptr) << error.message;

    std::vector<TableRow> rows;
    ASSERT_TRUE(source->readRows(10, rows, error)) << error.message;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(cellText(rows[0], 0), "1"); // polarity p is sorted first alphabetically ('p', 'ts', 'x', 'y')
}

TEST(Hdf5EventSource, ReadsCompatibleColumnGroupDirectlyAsEvents) {
    Hdf5MemoryFile hfile;
    hid_t gid = H5Gcreate2(hfile.id(), "/events", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    hsize_t dim = 3;
    auto column = [&](const char* name, hid_t type, const void* values) {
        hid_t space = H5Screate_simple(1, &dim, nullptr);
        hid_t dataset = H5Dcreate2(gid, name, type, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(dataset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, values);
        H5Dclose(dataset);
        H5Sclose(space);
    };
    const uint16_t xs[] = {1, 2, 3};
    const uint16_t ys[] = {4, 5, 6};
    const int64_t ts[] = {100, 200, 300};
    const int8_t ps[] = {-1, 0, 1};
    column("xs", H5T_NATIVE_UINT16, xs);
    column("ys", H5T_NATIVE_UINT16, ys);
    column("ts", H5T_NATIVE_INT64, ts);
    column("ps", H5T_NATIVE_INT8, ps);
    H5Gclose(gid);

    EventImportOptions options;
    options.format = EventSourceFormat::Hdf5;
    options.dataset = "/events";
    // Alphabetical group order: ps, ts, xs, ys.
    options.columns = {{2, 3, 1, 0}};
    ImportDiagnostic error;
    auto source = createHdf5SourceFromImage(hfile.getImage(), options, error);
    ASSERT_NE(source, nullptr) << error.describe();
    ASSERT_TRUE(source->readsEventsDirectly());
    std::vector<DVSEvent> events;
    ASSERT_TRUE(source->readEvents(10, events, error)) << error.describe();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[1].x, 2);
    EXPECT_EQ(events[1].y, 5);
    EXPECT_EQ(events[2].t, 300);
    EXPECT_FALSE(events[0].polarity);
    EXPECT_FALSE(events[1].polarity);
    EXPECT_TRUE(events[2].polarity);
}

TEST(Hdf5EventSource, ReadsScalarCsvStringDataset) {
    Hdf5MemoryFile hfile;
    hid_t fid = hfile.id();

    std::string csv_text = "x,y,ts,p\n10,20,500,1\n30,40,600,0\n";
    hid_t sid = H5Screate(H5S_SCALAR);
    hid_t tid = H5Tcopy(H5T_C_S1);
    H5Tset_size(tid, csv_text.size());
    hid_t did = H5Dcreate2(fid, "/csv_data", tid, sid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(did, tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, csv_text.c_str());
    H5Dclose(did);
    H5Tclose(tid);
    H5Sclose(sid);

    auto image = hfile.getImage();
    ImportDiagnostic error;
    EventImportOptions options;
    options.format = EventSourceFormat::Hdf5;
    options.dataset = "/csv_data";

    auto source = createHdf5SourceFromImage(image, options, error);
    ASSERT_NE(source, nullptr) << error.message;

    std::vector<TableRow> rows;
    ASSERT_TRUE(source->readRows(10, rows, error)) << error.message;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(cellText(rows[0], 0), "10");
    EXPECT_EQ(cellText(rows[1], 2), "600");
}
