// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// CSVFile - simple database manager for CSV files
//

#pragma once

class ErrorHandler;

class CSVFile
{
protected:
	struct Field;

public:
	CSVFile();
	~CSVFile();

	// "CSV-ify" a string: generate the quoted version of the value
	// suitable for insertion in a CSV file.  Invokes the callback 
	// one or more times to append text to the result.  Per the 
	// typical CSV formatting, if the input string contains no 
	// commas,double-quote marks, or newline characters (\n or \r), 
	// the value is passed through unchanged.  If it does contain 
	// any of those characters, the CSV formatted value is enclosed 
	// in double quotes, and any double quotes appearing within the
	// value are stuttered (so '"' in the source becomes '""' in
	// the output).  If len is -1, the value is treated as a null-
	// terminated string.  The callback returns true if we should
	// continue, false if we should abort due to an error.  The
	// function in turn returns true if no errors occurred.
	static bool CSVify(
		const TCHAR *str, size_t len, 
		std::function<bool(const TCHAR *segment, size_t len)> append);

	// CSV-ify a list of strings.  This generates a line (or portion
	// of a line) suitable for use in a CSV file, with the values
	// separated by commas.
	static bool CSVify(
		const std::list<TSTRING> &lst,
		std::function<bool(const TCHAR *segment, size_t len)> append);
	
	// Parse a value as a CSV field.  This takes a string in CSV
	// file format, as described above, and parses it into a list
	// of string values.
	static void ParseCSV(const TCHAR *str, size_t len, std::list<TSTRING> &lst);

	// set the filename
	void SetFile(const TCHAR *filename) { this->filename = filename; }

	// read the file into memory
	bool Read(ErrorHandler &eh, UINT mbCodePage = CP_ACP);

	// write the in-memory value set back to the file
	bool Write(ErrorHandler &eh);

	// write the file if it's dirty
	bool WriteIfDirty(ErrorHandler &eh) { return dirty ? Write(eh) : true; }

	// get the number of rows
	size_t GetNumRows() const { return rows.size(); }

	// add a blank row, returning the row number
	int CreateRow();

	// Column description
	class Column
	{
		friend class CSVFile;

	public:
		Column(CSVFile *csv, const TCHAR *name, int index) : csv(csv), name(name), index(index) { }
		virtual ~Column() { }

		// get the column name and index
		const TCHAR *GetName() const { return name.c_str(); }
		int GetIndex() const { return index; }

		// get the value from a row
		const TCHAR *Get(int row, const TCHAR *defaultVal = nullptr) const;
		int GetInt(int row, int defaultVal = 0) const;
		float GetFloat(int row, float defaultVal = 0.0f) const;
		bool GetBool(int row, bool defaultVal = false) const;

		// set the value in a row
		void Set(int row, const TCHAR *value) const;
		void Set(int row, int value) const;
		void Set(int row, float value) const;
		void SetBool(int row, bool value) const;

		// Get/set the client-defined "parsed" data object.  This is
		// a data object of type defined by the client that can be
		// stored with the field to represent the parsed form of the
		// field data.  We only provide the storage for the object
		// and otherwise treat it as opaque; it's up to the client
		// to define and set this value as desired.  We manage the
		// memory of the parsed data via a std::unique_ptr; the
		// client transfers ownership of the object to us when
		// setting it.
		class ParsedData
		{
		public:
			virtual ~ParsedData() { }
		};
		ParsedData *GetParsedData(int row) const;
		void SetParsedData(int row, ParsedData *data) const;

	protected:
		// get the raw Field for a row.column intersection
		Field *GetField(int rowIndex) const;
		Field *GetOrCreateField(int rowIndex) const;

		// my container CSV file
		CSVFile *csv;

		// column name
		TSTRING name;

		// column index
		int index;
	};

	// Define a column.  The client calls this to define the columns in
	// its schema.  This returns a Column accessor object that the client
	// can use to access the column field for a given row.
	Column *DefineColumn(const TCHAR *name);

protected:
	// filename
	TSTRING filename;

	// Column map, keyed by name.  This defines the schema.
	std::unordered_map<TSTRING, Column> columns;

	// In-memory field.  This stores the value of a single column value in
	// a single row.
	struct Field
	{
	public:
		Field(TCHAR *value) : value(nullptr), fileStorage(nullptr), fileStorageLen(0)
			{ Set(value); }

		Field(TCHAR *fileStorage, size_t fileStorageLen)
			: value(fileStorage), fileStorage(fileStorage), fileStorageLen(fileStorageLen) { }

		Field(Field &field) : value(nullptr), fileStorage(field.fileStorage), fileStorageLen(field.fileStorageLen)
			{ Set(field.value); }

		~Field() { Clear(); }

		const TCHAR *Get(const TCHAR *defaultVal = nullptr) const
			{ return value != nullptr ? value : defaultVal; }

		void Set(const TCHAR *val)
		{
			// If we can fit the value into the original file storage
			// area, reuse that space.  Otherwise, allocate new memory.
			Clear();
			if (val != nullptr)
			{
				size_t lenNeeded = _tcslen(val) + 1;
				if (lenNeeded <= fileStorageLen)
					_tcscpy_s(this->value = fileStorage, fileStorageLen, val);
				else
					_tcscpy_s(this->value = new TCHAR[lenNeeded], lenNeeded, val);
			}
		}

		// get/set the parsed data object
		Column::ParsedData *GetParsedData() const { return parsedData.get(); }
		void SetParsedData(Column::ParsedData *d) { parsedData.reset(d); }

	protected:
		// clear the value
		void Clear()
		{
			if (value != nullptr && value != fileStorage)
			{
				delete value;
				value = nullptr;
			}
		}

		// Pointer to the underlying value.  If the value hasn't been
		// changed since the underlying file was loaded, this points
		// directly to the file data.  Otherwise it points to our private
		// value store.
		TCHAR *value;

		// Original file storage area.  If the field was loaded from file
		// data, this points to the original file storage area.
		TCHAR *fileStorage;
		size_t fileStorageLen;

		// client-defined parsed data
		std::unique_ptr<Column::ParsedData> parsedData;
	};

	// In-memory row.  A row is a vector of field values in column index order.
	struct Row
	{
		std::vector<Field> fields;
	};

	// Row list
	std::vector<Row> rows;

	// Raw file contents
	std::unique_ptr<wchar_t> fileContents;

	// have we written field values since loading the file?
	bool dirty;
};

