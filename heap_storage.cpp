/**
 * @file heap_storage.cpp - Implementation of storage_engine with a heap file structure.
 * SlottedPage: DbBlock
 * HeapFile: DbFile
 * HeapTable: DbRelation
 *
 * @author Nolan Smith & Grady Diakubama
 * @see "Seattle University, CPSC5300, Summer 2019"
 */
#include "heap_storage.h"

/*
// constructor for slotted page
 takes in a fresh block and the id for it as well as a flag to tell if the intake block is new
*/
SlottedPage::SlottedPage(Dbt &block, BlockID block_id, bool is_new) : DbBlock(block, block_id, is_new) {
    if (is_new) {
        num_records = 0;
        end_free = DbBlock::BLOCK_SZ - 1;
        put_header();
    } else {
        get_header(num_records, end_free);
    }
}

/*
 * Add a new record to a data block and return its RecordID
 * */
RecordID SlottedPage::add(const Dbt *data) throw(DbBlockNoRoomError) {
    if (has_room(data->get_size())) {
        u16 id = ++num_records;
        u16 size = (u16) data->get_size();
        end_free -= size;
        u16 loc = end_free + 1;
        put_header();
        put_header(id, size, loc);
        memcpy(address(loc), data->get_data(), size);
        return id;
    }
    throw DbBlockNoRoomError("no room for new record");

}

/*
 * Takes in a record id, returns a Dbt with the address of the location of the Data ptr
 * */
Dbt *SlottedPage::get(RecordID record_id) {
    u16 size = 0, loc = 0;
    get_header(size, loc, record_id);
    return loc == 0 ? nullptr : new Dbt(address(loc), size);
}

/*
 * Updates the Block at the given record id with a new Dbt object
 * */
void SlottedPage::put(RecordID record_id, const Dbt &data) throw(DbBlockNoRoomError) {
    u16 size, loc = 0;
    get_header(size, loc, record_id);
    u16 new_size = (u16) data.get_size();
    if (new_size > size) {
        u16 extra_size = new_size - size;
        if (!has_room(extra_size)) {
            throw DbBlockNoRoomError("not enough room for new record");
        }
        slide(loc, loc - extra_size);
        memcpy(address(loc - extra_size), data.get_data(), new_size);
    } else {
        //we slide to take up the room the new block needs
        memcpy(address(loc), data.get_data(), new_size);
        slide(loc + new_size, loc + size);
    }
    get_header(size, loc, record_id);
    put_header(record_id, new_size, loc);
}


/*
 * Deletes the Data block based on record_id intake
 * */
void SlottedPage::del(RecordID record_id) {
    u16 size, loc = 0;
    get_header(size, loc, record_id);
    //if block doesnt exist
    if (loc == 0) {
        return;
    }
    slide(loc, loc + size);
    put_header(record_id, 0, 0);
}


/*
 * Create vector that holds all existing RecordID's
 * */
RecordIDs *SlottedPage::ids(void) {
    u16 size, loc = num_records;
    RecordIDs *result_holder = new RecordIDs();
    for (RecordID id = 1; id <= num_records; id++) {
        get_header(size, loc, id);
        //fill in the vector with all RecordID's present
        if (loc != 0) {
            result_holder->push_back(id);
        }
    }
    return result_holder;
}

/*
 * Gimme code from canvas
 * */
u16 SlottedPage::get_n(u16 offset) {
    return *(u16 *) address(offset);
}

void SlottedPage::put_n(u16 offset, u16 n) {
    *(u16 *) address(offset) = n;
}

// Make a void* pointer for a given offset into the data block.
void *SlottedPage::address(u16 offset) {
    return (void *) ((char *) block.get_data() + offset);
}

// Store the size and offset for given id. For id of zero, store the block header.
void SlottedPage::put_header(RecordID id, u16 size, u16 loc) {
    if (id == 0) {
        size = num_records;
        loc = end_free;
    }
    put_n(4 * id, size);
    put_n(4 * id + 2, loc);
}

/*
 * Get header based on record ID and put its size and loc into variables passed in
 * */
void SlottedPage::get_header(u_int16_t &size, u_int16_t &loc, RecordID id) {
    size = get_n(4 * id);
    loc = get_n(4 * id + 2);
}

/*
 * Check if the current block has room to add or put entries
 * */
bool SlottedPage::has_room(u_int16_t size) {
    u16 spaceInBlock = end_free - (num_records + 1) * 4;
    return size <= spaceInBlock;
}

/*
 * Shifts spots in addresses, copy content and fix up the headers when performing
 * changes.
 * */
void SlottedPage::slide(u_int16_t start, u_int16_t end) {

    u16 shift_range = end - start;
    if (shift_range > 0) {

        void *start_point = address((u16) (end_free + 1));
        void *destination = address((u16) (end_free + 1 + shift_range));

        unsigned size = (start - end_free + 1);
        char payload[size];
        //copy the content
        memcpy(payload, start_point, size);
        memcpy(destination, payload, size);

        RecordIDs *rIDList = ids();
        for (RecordID &record_id : *rIDList) {
            u16 size = 0, loc = 0;
            get_header(size, loc, record_id);
            if (loc <= start) {
                loc += shift_range;
                put_header(record_id, size, loc);
            }
        }
        delete rIDList;
        end_free += shift_range;
        put_header();
    }
}

/*
 * Heapfile Class
 * */

/*
 * Create new Heapfile with new block of storage
 * */
void HeapFile::create(void) {

    db_open(DB_CREATE | DB_EXCL);
    SlottedPage *block = get_new();
    put(block);
    delete block;
}

void HeapFile::drop(void) {
    close();
    remove(dbfilename.c_str());
}

void HeapFile::db_open(uint flags) {

    if (closed) {

        const char *eHome = nullptr;
        _DB_ENV->get_home(&eHome);
        std::string path = "../";
        this->dbfilename = path + eHome + "/" + name + ".db";

        db.open(nullptr, dbfilename.c_str(), nullptr, DB_RECNO, flags, 0);
        DB_BTREE_STAT *stat;

        db.stat(nullptr, &stat, DB_FAST_STAT);
        last = stat->bt_ndata;
        closed = false;
    }
    return;
}

void HeapFile::open(void) {
    db_open();
}

void HeapFile::close(void) {
    db.close(0);
    closed = true;
}

/*
 * Fn provided by instructor, creates a slottedpage with a new data block
 * */
SlottedPage *HeapFile::get_new(void) {

    char block[DbBlock::BLOCK_SZ];
    memset(block, 0, sizeof(block));
    Dbt data(block, sizeof(block));

    int block_id = ++last;
    Dbt key(&block_id, sizeof(block_id));

    // write out an empty block and read it back in so Berkeley DB is managing the memory
    SlottedPage *page = new SlottedPage(data, last, true);

    db.put(nullptr, &key, &data, 0);
    db.get(nullptr, &key, &data, 0);

    return page;
}

/*
 * Return the page associated with the given block id
 * */
SlottedPage *HeapFile::get(BlockID block_id) {

    char block[DbBlock::BLOCK_SZ];
    Dbt data(block, sizeof(block));
    Dbt key(&block_id, sizeof(block_id));
    db.get(nullptr, &key, &data, 0);
    SlottedPage *page = new SlottedPage(data, block_id, false);
    return page;
}

/*
 *
 * */
void HeapFile::put(DbBlock *block) {

    BlockID bID = block->get_block_id();
    void *currentData = block->get_data();
    Dbt key(&bID, sizeof(bID));
    Dbt data(currentData, DbBlock::BLOCK_SZ);
    db.put(nullptr, &key, &data, 0);
}

BlockIDs *HeapFile::block_ids() {

    BlockIDs *blocks = new BlockIDs;
    for (BlockID i = 1; i <= last; i++)
        blocks->push_back(i);
    return blocks;
}

/*
 * Constructor defined for compilation
 * */
HeapTable::HeapTable(Identifier table_name, ColumnNames column_names, ColumnAttributes column_attributes) :
        DbRelation(table_name, column_names, column_attributes), file(table_name) {}

void HeapTable::create() {
    file.create();
}

void HeapTable::create_if_not_exists() {
    try {
        open();
    } catch (DbException &e) {
        create();
    }
}

/*
 * Drop calls Heapfile to drop a table
 * */
void HeapTable::drop() {
    file.drop();
}

void HeapTable::open() {
    file.open();
}

void HeapTable::close() {
    file.close();
}

/*
 * Insert a new row given a ValueDict that is a key/val pair
 * */
Handle HeapTable::insert(const ValueDict *row) {
    open();
    //check to make sure row is valid before appending
    return append(validate(row));
}

void HeapTable::update(const Handle handle, const ValueDict *new_values) {
    //TO-BE IMPLEMENTED
}

/*
 *
 * */
void HeapTable::del(const Handle handle) {
    open();
    SlottedPage *target = file.get(handle.first);
    target->del(handle.second);
    file.put(target);
    delete target;
}


// Usage: return everything in the block
// @param: None
// @return: Handle pointer
Handles *HeapTable::select() {

    Handles *hndls = new Handles();
    BlockIDs *bIDs = file.block_ids();

    for (BlockID bID: *bIDs) {
        SlottedPage *curr_block = file.get(bID);
        RecordIDs *rIDs = curr_block->ids();
        for (RecordID &rID : *rIDs) {
            Handle h;
            h.first = bID;
            h.second = rID;
            hndls->push_back(h);
        }
    }
    return hndls;
}

/*
 * executes a select where call to DB
 * */
Handles *HeapTable::select(const ValueDict *where) {
    open();
    Handles *handle_list = new Handles(); //hold the return data, is a vector based on the define in storage_engine.h
    BlockIDs *bIDs = file.block_ids();
    for (BlockID &bID : *bIDs) {
        SlottedPage *curr_block = file.get(bID);
        RecordIDs *rIDs = curr_block->ids();
        for (RecordID &rID : *rIDs) {
            Handle temp(bID, rIDs->at(rID));
            handle_list->push_back(temp);
        }
    }
    delete bIDs;
    return handle_list;
}


ValueDict *HeapTable::project(Handle handle) {
    return project(handle, &column_names);
}

/*
 * Gets data based on handles block id and record id
 * gets the block associated with the block id
 * checks to see if the db entry associated with the given input exists
 * returns the results
 * */
ValueDict *HeapTable::project(Handle handle, const ColumnNames *column_names) {
    //This is a pair so that we need to use first and second to get access
    BlockID block_id = handle.first;
    RecordID record_id = handle.second;
    SlottedPage *curr_block = file.get(block_id);
    Dbt *curr_data = curr_block->get(record_id);
    ValueDict *curr_row = unmarshal(curr_data);
    delete curr_data;
    delete curr_block;
    if (column_names->empty()) {
        return curr_row;
    }
    ValueDict *result = new ValueDict();
    for (Identifier const &column_name: *column_names) {
        if (curr_row->find(column_name) == curr_row->end()) {
            throw DbRelationError("Table does not contain column: " + column_name + "'");
        }
        (*result)[column_name] = (*curr_row)[column_name];
    }
    delete curr_row;
    return result;
}


ValueDict *HeapTable::validate(const ValueDict *row) {
    ValueDict *WholeRow = new ValueDict();
    for (auto &column_name: column_names) {
        Value value;
        ValueDict::const_iterator column = row->find(column_name);
        if (column == row->end()) {
            throw DbRelationError("column does not exist");
        } else {
            value = column->second;
        }
        (*WholeRow)[column_name] = value;
    }
    return WholeRow;
}


/*
 * Gets data block from DB and adds to it returning a new handle
 * */
Handle HeapTable::append(const ValueDict *row) {
    Dbt *data = marshal(row);
    SlottedPage *block = file.get(file.get_last_block_id());
    RecordID record_id;
    try {
        record_id = block->add(data);
    } catch (DbBlockNoRoomError &e) {
        // need a new block
        block = file.get_new();
        record_id = block->add(data);
    }
    file.put(block);
    delete block;
    delete[] (char *) data->get_data();
    delete data;
    return Handle(file.get_last_block_id(), record_id);
}

/*
 * Gimme code from canvas which creates a Dbt object based on a ValueDict
 * */
Dbt *HeapTable::marshal(const ValueDict *row) {
    char *bytes = new char[DbBlock::BLOCK_SZ];
    uint offset = 0;
    uint col_num = 0;
    for (auto const &column_name: column_names) {
        ColumnAttribute ca = column_attributes[col_num++];
        ValueDict::const_iterator column = row->find(column_name);
        Value value = column->second;
        if (ca.get_data_type() == ColumnAttribute::DataType::INT) {
            *(int32_t *) (bytes + offset) = value.n;
            offset += sizeof(int32_t);
        } else if (ca.get_data_type() == ColumnAttribute::DataType::TEXT) {
            uint size = value.s.length();
            *(u16 *) (bytes + offset) = size;
            offset += sizeof(u16);
            memcpy(bytes + offset, value.s.c_str(), size); // assume ascii for now
            offset += size;
        } else {
            throw DbRelationError("Only know how to marshal INT and TEXT");
        }
    }
    char *right_size_bytes = new char[offset];
    memcpy(right_size_bytes, bytes, offset);
    delete[] bytes;
    Dbt *data = new Dbt(right_size_bytes, offset);
    return data;
}

/*
 * Returns the row with human readable info based on the data given.
 * The opposite to marshal.
 * */
ValueDict *HeapTable::unmarshal(Dbt *data) {
    ValueDict *row = new ValueDict();
    Value value;
    char *bytes = (char *) data->get_data();
    unsigned offset = 0, col_num = 0;
    for (Identifier const &column_name: column_names) {
        ColumnAttribute ca = column_attributes[col_num++];
        value.data_type = ca.get_data_type();
        if (ca.get_data_type() == ColumnAttribute::DataType::INT) {
            value.n = *(u32 *) (bytes + offset);
            offset += sizeof(u32);
        } else if (ca.get_data_type() == ColumnAttribute::DataType::TEXT) {
            u16 size = *(u16 *) (bytes + offset);
            offset += sizeof(u16);
            char buffer[DbBlock::BLOCK_SZ];
            memcpy(buffer, bytes + offset, size);
            buffer[size] = '\0';
            value.s = std::string(buffer);
            offset += size;
        } else {
            throw DbRelationError("Only know how to unmarshal INT, TEXT");
        }
        (*row)[column_name] = value;
    }
    return row;
}

bool test_heap_storage() {
    ColumnNames column_names;
    column_names.push_back("a");
    column_names.push_back("b");
    ColumnAttributes column_attributes;
    ColumnAttribute ca(ColumnAttribute::INT);
    column_attributes.push_back(ca);
    ca.set_data_type(ColumnAttribute::TEXT);
    column_attributes.push_back(ca);
    HeapTable table1("_test_create_drop_cpp", column_names, column_attributes);
    table1.create();
    std::cout << "create ok" << std::endl;
    table1.drop();
    std::cout << "drop ok" << std::endl;

    HeapTable table("_test_data_cpp", column_names, column_attributes);
    table.create_if_not_exists();
    std::cout << "create_if_not_exsts ok" << std::endl;

    ValueDict row;
    row["a"] = Value(12);
    row["b"] = Value("Hello!");
    std::cout << "try insert" << std::endl;
    table.insert(&row);
    std::cout << "insert ok" << std::endl;
    Handles *handles = table.select();
    std::cout << "select ok " << handles->size() << std::endl;
    ValueDict *result = table.project((*handles)[0]);
    std::cout << "project ok" << std::endl;
    Value value = (*result)["a"];
    if (value.n != 12)
        return false;
    value = (*result)["b"];
    if (value.s != "Hello!")
        return false;
    table.drop();

    cout << "Program tested Successfully" << endl;
    return true;
}
