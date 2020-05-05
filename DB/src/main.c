/*
This script contains c implementation of database using btrees.
*/

/*
Lets follow the following structure for writing this script.
1. All Libraries to be included
2. All Constants declaration.
3. All Struct definitions.
4. Functons.

*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

#define MAX(a,b) ((a) > (b) ? (a) : (b))

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;


// Error codes.
enum PrepareResult_t {
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT,
    MAXIMUM_PAGES_REACHED
};

// All constants would go here.
const int PAGE_SIZE = 4096;

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef struct{
    uint32_t id;
    // Increasing length of name and email by one, so that it can accommodate the end of string charecter.
    char name[COLUMN_USERNAME_SIZE+1];
    char email[COLUMN_EMAIL_SIZE+1];
} Row;

const int ID_SIZE = sizeof(((Row*)0)->id);
const int NAME_SIZE = sizeof(((Row*)0)->name);
const int EMAIL_SIZE = sizeof(((Row*)0)->email);
const int ROW_SIZE = ID_SIZE + NAME_SIZE + EMAIL_SIZE;

const int id_offset = 0;
const int name_offset = id_offset + ID_SIZE;
const int email_offset = name_offset + NAME_SIZE;


typedef struct Pager
{
    void* page;
    uint8_t node_type;
    uint8_t is_root;
    size_t num_of_rows;
    short parent_pagenumber;
    short page_number;
} Pager;

// Defining sizes and offsets for different attributes of a page.
const uint32_t NODE_TYPE_SIZE = sizeof(((Pager*)0)->node_type);
const uint32_t IS_ROOT_SIZE = sizeof(((Pager*)0)->is_root);
const uint32_t NUM_OF_ROWS_SIZE = sizeof(((Pager*)0)->num_of_rows);
const uint32_t PARENT_POINTER_SIZE = sizeof(((Pager*)0)->parent_pagenumber);
const uint32_t PAGE_NUMBER_SIZE = sizeof(((Pager*)0)->page_number);

// OFFSETS
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_OFFSET =  NODE_TYPE_OFFSET + NODE_TYPE_SIZE;
const uint32_t NUM_OF_ROWS_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint32_t PARENT_POINTER_OFFSET = NUM_OF_ROWS_OFFSET + NUM_OF_ROWS_SIZE;
const uint32_t PAGE_NUMBER_OFFSET = PARENT_POINTER_OFFSET + PARENT_POINTER_SIZE;
const uint32_t SIZE_HEADER = NODE_TYPE_SIZE + IS_ROOT_SIZE + NUM_OF_ROWS_SIZE + PARENT_POINTER_OFFSET + PAGE_NUMBER_SIZE;
const uint32_t DATA_OFFSET = SIZE_HEADER;


typedef struct{
    short root_page_number;
    short number_of_rows_in_table;
    Pager* pager;
} Table;

typedef struct
{
    short row_number;
    short page_number;
    Table* table;
} Cursor;

Pager* initialise_pager(){
    Pager* page = malloc(sizeof((Pager*)0));
    page->page_number = -1;
    page->page = malloc(PAGE_SIZE);
    page->node_type = NODE_INTERNAL;
    page->is_root = 0;
    page->num_of_rows = 0;
    page->parent_pagenumber = -1;
    return page;
}

void free_object(void* object){
    free(object);
}

Row* get_row(){
    Row* row = NULL;
    row = malloc(sizeof(Row));
    return row;
}

Table* initialise_table(){
    Table* table = malloc(sizeof((Table*)0));
    table->root_page_number = -1;
    table->number_of_rows_in_table = 0;
    table->pager = initialise_pager();
    return table;
}

void serialize(Row* source, void* destination){
    /*
    This function serialises data. Copies data from source which is of type row
    to destination address starting with destination.
    */
    memcpy(destination+id_offset, &(source->id), ID_SIZE);
    memcpy(destination+name_offset, source->name, NAME_SIZE);
    memcpy(destination+email_offset, source->email, EMAIL_SIZE);
}

void desrialise(void* source, Row* destination){
    /*
    This function Copies data from address location starting with source to Row*
    */
    memcpy(&(destination->id), source+id_offset, ID_SIZE);
    memcpy(&(destination->name), source+name_offset, NAME_SIZE);
    memcpy(&(destination->email), source+email_offset, EMAIL_SIZE);

}

Pager* loadpage(short page_number, FILE* fptr){
    // This function loads a page specific page number to memory and returns the page.
    // If page number is not present it creates a new one and returns it.
    int len;
    fseek(fptr, 0, SEEK_END);
    long len_of_file = ftell(fptr);
    // Assuming page numbers starts from 0. Subtracting 1 from number of pages.
    long index_of_last_page = (len_of_file/PAGE_SIZE) - 1;

    // Checking if new page needs to be created. That too if required page number should be equal to last page's number
    // +1

    Pager* page = initialise_pager();

    if (page_number > index_of_last_page){
        page->page_number = page_number;
        return page;
    }

    // Moves Pointer to start of the req. Page number
    fseek(fptr, page_number*PAGE_SIZE, SEEK_SET);

    // Copies the data from file to page->page
    fread(page->page, PAGE_SIZE, 1, fptr);

    memcpy(&page->node_type, page->page + NODE_TYPE_OFFSET, NODE_TYPE_SIZE);
    memcpy(&page->is_root, page->page + IS_ROOT_OFFSET, IS_ROOT_SIZE);
    memcpy(&page->num_of_rows, page->page + NUM_OF_ROWS_OFFSET, NUM_OF_ROWS_SIZE);
    memcpy(&page->parent_pagenumber, page->page + PARENT_POINTER_OFFSET, PARENT_POINTER_SIZE);
    memcpy(&page->page_number, page->page + PAGE_NUMBER_OFFSET, PAGE_NUMBER_SIZE);
//    page->page_number = page_number;
    return page;
}

Cursor* table_start(Table* table){
    // This function initialises cursor to point to the root of the table.
    Cursor* cursor = malloc(sizeof((Cursor*)0));
    cursor->table = table;
    // Load the root page.
    cursor->page_number = table->root_page_number;
    cursor->row_number = 0;
    cursor->page_number = table->root_page_number;
    return cursor;
}

void write_table_data_to_file(Table* table, FILE* fptr){
    if(table->number_of_rows_in_table > 0){
        // Checking if new rows are inserted into DB or not
        fseek(fptr, 0, SEEK_SET);
        fwrite(&table->root_page_number, sizeof(table->root_page_number), 1, fptr);
        fseek(fptr, sizeof(table->root_page_number), SEEK_SET);
        fwrite(&table->number_of_rows_in_table, sizeof(table->number_of_rows_in_table), 1, fptr);

        // Moving Pointer to root page.
        fseek(fptr, table->pager->page_number * PAGE_SIZE, SEEK_SET);
        fwrite(table->pager->page, PAGE_SIZE, 1, fptr);

        // writing contents of root node.

        fseek(fptr, table->root_page_number * PAGE_SIZE + NODE_TYPE_OFFSET, SEEK_SET);
        fwrite(&table->pager->node_type, NODE_TYPE_SIZE, 1, fptr);
        fseek(fptr, table->root_page_number * PAGE_SIZE + IS_ROOT_OFFSET, SEEK_SET);
        fwrite(&table->pager->is_root, IS_ROOT_SIZE, 1, fptr);
        fseek(fptr, table->root_page_number * PAGE_SIZE + NUM_OF_ROWS_OFFSET, SEEK_SET);
        fwrite(&table->pager->num_of_rows, NUM_OF_ROWS_SIZE, 1, fptr);
        fseek(fptr, table->root_page_number * PAGE_SIZE + PARENT_POINTER_OFFSET, SEEK_SET);
        fwrite(&table->pager->parent_pagenumber, PARENT_POINTER_SIZE, 1, fptr);
        fseek(fptr, table->root_page_number * PAGE_SIZE + PAGE_NUMBER_OFFSET, SEEK_SET);
        fwrite(&table->pager->page_number, PAGE_NUMBER_SIZE, 1, fptr);
    }
}

void* cursor_value(Cursor* cursor, FILE* fptr){

    // Checking if page is already loaded.
    if(cursor->page_number != cursor->table->pager->page_number){
        cursor->table->pager = loadpage(cursor->page_number, fptr);
    }

    // Checking if maximum number of rows are inserted already
    int max_number_of_rows_in_a_page = (PAGE_SIZE - DATA_OFFSET)/ROW_SIZE;
    if (cursor->row_number >= max_number_of_rows_in_a_page){
        printf("Maximum number of rows have been inserted\n");
        write_table_data_to_file(cursor->table, fptr);
        exit(EXIT_FAILURE);
    }

    printf("%s%d\n", "rownumber:", cursor->row_number);
    uint32_t row_number_address = (cursor->row_number * ROW_SIZE);
    printf("%s:%p", "Page Address", cursor->table->pager->page);
    printf("%s:%d", "Data Offset", DATA_OFFSET);
    printf("%s:%d", "Row Number Address", row_number_address);
    printf("%s:%p", "Cursor Value", cursor->table->pager->page + DATA_OFFSET + row_number_address);
    return cursor->table->pager->page + DATA_OFFSET + row_number_address;
}

Cursor* table_iterator(int key, Table* table, FILE* fptr){
    // This function iterates through the table and finds the correct position where key should be inserted and
    // points the cursor accordingly.
    Cursor* cursor = malloc(sizeof((Cursor*)0));
    cursor->table = table;
    cursor->page_number = table->root_page_number;
    if(cursor->page_number != cursor->table->pager->page_number){
        cursor->table->pager = loadpage(cursor->page_number, fptr);
    }
    Row* row = get_row();
    short max_index_row = (cursor->table->pager->num_of_rows - 1);
    short min_row_number = 0;
    short max_row_number = max_index_row;

    short mid;
    // Binary Search code goes here.
    // Following code finds the whether the given key is present or not, if its not present,
    // it finds the number which is just higher that the key value.
    while (min_row_number < max_row_number){
        mid = floor((min_row_number + max_row_number)/2);
        desrialise(cursor->table->pager->page + DATA_OFFSET + (mid * ROW_SIZE), row);
        if (key > row->id){
            min_row_number = mid + 1;
        }
        else if (key < row->id){
            max_row_number = mid;
        }
        else{
            // Checking if key is already present.
            printf("Duplicate Primary Key Error\n");
            write_table_data_to_file(cursor->table, fptr);
            exit(EXIT_FAILURE);
        }
    }

    // Checking if there are no rows present.
    if (max_row_number == -1){
        free_object(row);
        cursor->row_number = 0;
        return cursor;
    }

    // Checking if key is greater than last element also.
    desrialise(cursor->table->pager->page + DATA_OFFSET + (max_index_row * ROW_SIZE), row);

    if (key > row->id){
        // Making max_row_number point next index.
        max_row_number += 1;
    }

    // Checking if key is same as last element.
    if (key == row->id){
        printf("Duplicate Primary Key Error\n");
        write_table_data_to_file(cursor->table, fptr);
        free_object(row);
        exit(EXIT_FAILURE);
    }

    // Moving the data to make space for new entry.
    if (max_row_number <=  max_index_row){
        short num_rows_to_be_copied = (max_index_row - max_row_number + 1);
        memcpy(cursor->table->pager->page + DATA_OFFSET + ((max_row_number+1) * ROW_SIZE), cursor->table->pager->page + DATA_OFFSET + (max_row_number * ROW_SIZE), (num_rows_to_be_copied * ROW_SIZE));
    }

    free_object(row);
    cursor->row_number = max_row_number;
    return cursor;
}

InputBuffer* get_inputbuffer(){
    InputBuffer* input_buffer = NULL;
    input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;
    return input_buffer;
}


void read_input_buffer(InputBuffer* input_buffer){
    input_buffer->input_length = getline(&(input_buffer->buffer), &(input_buffer->buffer_length),  stdin);
    // Strip()
    input_buffer->buffer[input_buffer->input_length - 1] = 0;
    input_buffer->input_length -= 1;
}


void print_prompt(){
    printf("db > ");
}

int process_non_sql_statements(char* statement){
    if (strcmp(statement, ".exit") == 0){
        printf("Closing DB connection");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

int insert_row_into_table(Row* row, Table* table, FILE* fptr){
    /*
    This function finds the corresponding page and corresponding memory location in the page to copy the row data, using
    serialising function. This function also allocates memory to the page.
    */

    // Checking for errors.

    if (strlen(row->name) > COLUMN_USERNAME_SIZE || strlen(row->email) > COLUMN_EMAIL_SIZE){
        printf("%s\n", "Check the length of fields supplied for Name and Email columns.");
        return PREPARE_STRING_TOO_LONG;
    }
    Cursor* cursor = table_iterator(row->id, table, fptr);
    serialize(row, cursor_value(cursor, fptr));
    table->pager->num_of_rows += 1;
    table->number_of_rows_in_table += 1;
    printf("%s:%zu\n", "num_of_rows in page", table->pager->num_of_rows);
    printf("%s:%hd\n", "Page Number", table->pager->page_number);
    printf("%s:%hd\n", "Parent Page Number", table->pager->parent_pagenumber);
    printf("%s:%d\n", "Row inserted", table->number_of_rows_in_table);
    free_object(cursor);
    return EXIT_SUCCESS;
}


void print_all_rows(Row* row, Table* table, FILE* fptr){
    short start_row;
    Cursor* cursor = table_start(table);
    for(start_row=0; start_row < table->number_of_rows_in_table; start_row++){
        // Loading the page if its not loaded into the table yet.
        cursor->row_number = start_row;
        desrialise(cursor_value(cursor, fptr), row);
        printf("%d,%s,%s\n", row->id, row->name, row->email);
    }
    free_object(cursor);
}

int process_sql_statements(char* statement, Table* table, FILE* fptr){
    if (strncmp(statement, "insert", 6) == 0){
        printf("Insert statement will be processed\n");
        Row* row = get_row();
        int args_assigned = sscanf(statement, "insert %d %s %s", &(row->id), row->name, row->email);

        if (args_assigned != 3){
            printf("Synatx Error\n");
            return PREPARE_SYNTAX_ERROR;
        }
        // Insert row into the table.
        insert_row_into_table(row, table, fptr);

        free_object(row);
        return EXIT_SUCCESS;

    }
        // Adding support to print all the rows
    else if(strncmp(statement, "select_all", 10) == 0){
        Row* row = get_row();
        print_all_rows(row, table, fptr);
        free_object(row);
        return EXIT_SUCCESS;
    }

    else if(strncmp(statement, "select", 6) == 0){
        printf("Select statement will be processed\n");
        Row* row = get_row();
        int id;
        int args_assigned = sscanf(statement, "select %d", &id);
        if(args_assigned != 1){
            printf("More or less number of arguments in the query statement\n");
            return EXIT_FAILURE;
        }
        // fetch_row_from_table(id, row, table);
        free_object(row);
        return EXIT_SUCCESS;
    }
    else{
        return EXIT_FAILURE;
    }
}

void free_buffer(InputBuffer* input_buffer){
    free(input_buffer->buffer);
    free(input_buffer);
}

int main(void)
{
    InputBuffer* input_buffer = NULL;
    input_buffer = get_inputbuffer();

    int max_number_of_rows_in_a_page = (PAGE_SIZE - DATA_OFFSET)/ROW_SIZE;
    printf("%s:%d", "Max number of rows in a page", max_number_of_rows_in_a_page);
    printf("\n");
    FILE *fptr;
    fptr = fopen("/Users/demo/Downloads/santosh/Database/Database_btree.db", "r+");
    if (fptr == NULL)
    {
        fptr = fopen("/Users/demo/Downloads/santosh/Database/Database_btree.db", "w+");
    }
    int len;
    fseek(fptr, 0, SEEK_END);
    long len_of_file = ftell(fptr);

    Table* table = initialise_table();


    if(len_of_file != 0){
        // Checking if there are any contents in the file or its a fresh start.
        // If there is some data, load root page number and number of rows in the table.
        fseek(fptr, 0, SEEK_SET);
        fread(&table->root_page_number, sizeof(table->root_page_number), 1, fptr);
        fseek(fptr, sizeof(table->root_page_number), SEEK_SET);
        fread(&table->number_of_rows_in_table, sizeof(table->number_of_rows_in_table), 1, fptr);
    }
    else{
        table->root_page_number = 1;
    }

    while(true){

        print_prompt();

        read_input_buffer(input_buffer);

        if (input_buffer->input_length == 0) {
            printf("Error reading input\n");
            free_buffer(input_buffer);
            continue;
        }

        if (input_buffer->buffer[0] ==  '.'){
            if (process_non_sql_statements(input_buffer->buffer) == EXIT_SUCCESS){
                free_buffer(input_buffer);
                // Write to File.
                write_table_data_to_file(table, fptr);
                free(table);
                fclose(fptr);
                exit(EXIT_SUCCESS);
            }
            else{
                printf("Unknown statement\n");
                continue;
            }
        }
        else{
            printf("%s:%d\n", "Root Page Number is", table->root_page_number);
            if (process_sql_statements(input_buffer->buffer, table, fptr) == EXIT_SUCCESS){
                continue;
            }
            else{
                printf("Something Wrong\n");
                continue;
            }
        }
        free_buffer(input_buffer);


    }

    return 0;
}