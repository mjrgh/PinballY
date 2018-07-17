// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "Resource.h"
#include "CSVFile.h"

CSVFile::CSVFile() : dirty(false)
{
}

CSVFile::~CSVFile()
{
}

CSVFile::Column *CSVFile::DefineColumn(const TCHAR *name)
{
	// look for an existing column of the same name
	if (auto it = columns.find(name); it != columns.end())
		return &it->second;

	// it's not there yet - add a new column
	auto it = columns.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(name),
		std::forward_as_tuple(this, name, columns.size()));
	return &it.first->second;
}

bool CSVFile::Read(ErrorHandler &eh, UINT mbCodePage)
{
	// read the file into memory
	long fileLen;
	wchar_t *contents = ReadFileAsWStr(filename.c_str(), eh, fileLen, ReadFileAsStr_NullTerm, mbCodePage);

	// fail if we didn't load the file
	if (contents == nullptr)
		return false;

	// clear all existing rows
	rows.clear();

	// we're now synced with the disk verison
	dirty = false;

	// store the contents in our internal content pointer
	fileContents.reset(contents);

	// Parse a field
	wchar_t *p = contents;
	bool eol = false;
	bool eof = false;
	size_t fieldLen = 0;
	auto ParseField = [&p, &eol, &eof, &fieldLen]()
	{
		// note the starting position
		wchar_t *start = p;

		// presume we won't reach the end of the line or end of file
		eol = false;
		eof = false;

		// check for a quote
		if (*p == '"')
		{
			// It's a quoted value.  Find the closing quote.  As we go,
			// strip out the enclosing quotes and any embedded stuttered
			// quotes.
			wchar_t *dst = p++;
			for (;;)
			{
				// check for a quote
				if (*p == '"')
				{
					// Skip the quote.  If it's not stuttered, it's our closing quote,
					// so stop here.  If another quote immediately follows, keep the
					// second quote - the "" sequence turns into a single ".
					if (*++p != '"')
						break;
				}

				// stop at end of file
				if (*p == 0)
					break;

				// copy this character and continue
				*dst++ = *p++;
			}

			// null-terminate the result string
			*dst = 0;

			// Skip the closing quote
			if (*p == '"')
				++p;

			// If we're not at a newline, comma, or end of file, the quoted
			// item is ill-formed.  Simply skip any intervening characters
			// until the next separator.
			for (; *p != ',' && *p != 0 && *p != 10 && *p != 13; ++p);

			// If we're not at end-of-file, we're at a separator.  Skip it
			// so that we're positioned at the start of the next field.
			// If we're at a newline, skip all consecutive newlines.
			if (*p == ',')
				++p;
			else if (*p == 0)
				eof = eol = true;
			else
				for (eol = true; *p == 10 || *p == 13; ++p);
		}
		else
		{
			// It's not quoted.  Parse everything up to the end of the
			// field - comma, newline, or end of file.
			for (; *p != ',' && *p != 0 && *p != 10 && *p != 13; ++p);

			// we can use everything up to the separator for field storage
			fieldLen = p - start;

			// if we're at a comma or end-of-line, replace it with
			// a null terminator and skip it
			if (*p == ',')
				*p++ = 0;
			else if (*p == 0)
				eol = eof = true;
			else
				for (eol = true, *p++ = 0; *p == 10 || *p == 13; ++p);
		}

		// we can use everything up to the start of the next field for
		// updated storage
		fieldLen = p - start;

		// return the start of the field
		return start;
	};

	// skip any leading blank lines
	for (; *p == 10 || *p == 13; ++p);
	if (*p == 0)
		return true;

	// The first line of a CSV is the column list.  Parse it.  For each
	// column, determine if the column exists in our current column set:
	// if so, set the existing column's index to match the file order; 
	// if not, add the new column.
	for (int colno = 0; !eol; ++colno)
	{
		// parse a field
		wchar_t *colname = ParseField();

		// look it up
		Column *col = DefineColumn(colname);

		// set the column index to match the file layout
		col->index = colno;
	}

	// Now parse each line
	while (!eof)
	{
		// skip blank lines
		for (; *p == 10 || *p == 13; ++p);
		if (*p == 0)
			return true;

		// create a new row
		rows.emplace_back();
		Row &row = rows.back();

		// parse the fields
		for (eol = false; !eol ;)
		{
			wchar_t *val = ParseField();
			row.fields.emplace_back(val, fieldLen);
		}
	}

	// success
	return true;
}

bool CSVFile::Write(ErrorHandler &eh)
{
	// open the file
	FILE *fp = nullptr;
	if (int err = _tfopen_s(&fp, filename.c_str(), _T("w,ccs=UTF-16LE")); err != 0)
	{
		eh.Error(MsgFmt(IDS_ERR_OPENFILE, filename, FileErrorMessage(err).c_str()));
		return false;
	}

	// report a write error and return false
	auto ReportError = [&eh, this](int err)
	{
		eh.Error(MsgFmt(IDS_ERR_WRITEFILE, filename, FileErrorMessage(err).c_str()));
		return false;
	};

	// We need to write the column list in column index order, so build
	// a vector of the columns by index.
	std::vector<Column*> colByIndex;
	colByIndex.resize(columns.size());
	for (auto &c : columns)
		colByIndex[c.second.index] = &c.second;

	// write a string segment to the file
	auto WriteSegment = [fp, ReportError](const TCHAR *str, size_t len)
	{
		if (_ftprintf(fp, _T("%.*s"), len, str) < 0)
			return ReportError(errno);
		else
			return true;
	};

	// Write the column list as the first line
	const TCHAR *comma = _T("");
	for (auto c : colByIndex)
	{
		// write the field separator, if any
		if (_ftprintf(fp, comma) < 0)
			return ReportError(errno);

		// we'll need a comma before the next column
		comma = _T(",");
		
		// write the column name
		if (!CSVify(c->GetName(), -1, WriteSegment))
			return false;
	}

	// end the line
	if (_ftprintf(fp, _T("\n")) < 0)
		return ReportError(errno);

	// Write each row
	for (auto const &row : rows)
	{
		// write the row's fields
		comma = _T("");
		for (auto const &field : row.fields)
		{
			// write the field separator, if any
			if (_ftprintf(fp, comma) < 0)
				return ReportError(errno);

			// write the column value
			if (!CSVify(field.Get(), -1, WriteSegment))
				return false;

			// we'll need a comma before the next column
			comma = _T(",");
		}

		// add a newline after the row
		if (_ftprintf(fp, _T("\n")) < 0)
			return ReportError(errno);
	}

	// close the file
	if (fclose(fp) < 0)
		return ReportError(errno);

	// all nice and clean
	dirty = false;

	// success
	return true;
}

bool CSVFile::CSVify(const std::list<TSTRING> &lst, std::function<bool(const TCHAR *, size_t)> append)
{
	// write the row's fields
	bool comma = false;
	for (auto const &str : lst)
	{
		// write the field separator, if any
		if (comma && !append(_T(","), 1))
			return false;

		// we'll need a comma before the next column, if there is one
		comma = true;

		// write the column value
		if (!CSVify(str.c_str(), -1, append))
			return false;
	}

	// success
	return true;
}

bool CSVFile::CSVify(const TCHAR *str, size_t len, std::function<bool(const TCHAR *, size_t)> append)
{
	// if it's a null string, write nothing to leave it as an empty string
	if (str == nullptr)
		return true;

	// if the length is to be inferred from null-termintaion, infer it
	if (len == -1)
		len = _tcslen(str);

	// figure the end pointer
	const TCHAR *endp = str + len;

	// scan for characters requiring quoting: commas, quotes, and newlines
	bool needQuotes = false;
	for (const TCHAR *p = str; p != endp; ++p)
	{
		if (*p == ',' || *p == 10 || *p == 13 || *p == '"')
		{
			needQuotes = true;
			break;
		}
	}

	// write it with or without quotes as needed
	if (needQuotes)
	{
		// write the open quote
		if (!append(_T("\""), 1))
			return false;

		// write each segment between double quotes
		while (str != endp)
		{
			// find the next double quote
			const TCHAR *start;
			for (start = str; *str != '"' && str != endp; ++str);

			// write this chunk
			if (!append(start, str - start))
				return false;

			// check for a quote
			if (*str == '"')
			{
				// write the stuttered quote
				if (!append(_T("\"\""), 2))
					return false;

				// skip the quote
				++str;
			}
		}

		// write the close quote, and we're done
		if (!append(_T("\""), 1))
			return false;
	}
	else
	{
		// no quotes required - just write the string exactly as-is
		if (!append(str, len))
			return false;
	}

	// success
	return true;
}

void CSVFile::ParseCSV(const TCHAR *str, size_t len, std::list<TSTRING> &lst)
{
	// If it's a null string, there's nothing to parse: simply return
	// the empty list.  Note that a null string will yield an empty
	// list, whereas a non-null pointer to a zero-character string 
	// will yield a list with one (empty string) element.
	if (str == nullptr)
		return;

	// if the length is to be inferred from null-termination, infer it
	if (len == -1)
		len = _tcslen(str);

	// figure the end pointer
	const TCHAR *endp = str + len;

	// set up an empty buffer for the current value
	TSTRING buf;

	// start the first value at the start of the buffer
	const TCHAR *start = str;

	// we're not in a quoted section yet
	bool inQuote = false;

	// parse each value
	for (;;)
	{
		// An unquoted comma, an unquoted newline, or the end of the overall 
		// string ends the current field.
		// The end of the string overall also ends the field.
		if ((!inQuote && (*str == ',' || *str == 10 || *str == 13)) || str == endp)
		{
			// append the current section to the buffer
			buf.append(start, str - start);

			// add this value to the result list
			lst.emplace_back(buf);

			// if this is the end of the string, we're done
			if (str == endp)
				break;

			// skip the comma and start the next field
			start = ++str;
			buf.clear();
			continue;
		}

		// Check for a quote
		if (*str == '"')
		{
			// check if we're already in a quoted section
			if (inQuote)
			{
				// We're currently in a quoted section.  If this quote is stuttered,
				// it translates to one quote in the result string.  Otherwise it
				// ends the current quoted section.
				if (str + 1 != endp && *(str + 1) == '"')
				{
					// stuttered quote - add the part up to and including the quote
					// to the result buffer
					++str;
					buf.append(start, str - start);

					// skip the repeated quote and continue from here
					start = ++str;
					continue;
				}
				else
				{
					// It's not stuttered, so this ends the quoted section.  Add
					// the part up to (but not including) the quote to the buffer.
					buf.append(start, str - start);

					// we're no longer in a quoted section
					inQuote = false;

					// skip the quote and continue from here
					start = ++str;
					continue;
				}
			}
			else
			{
				// entering a quoted section - add the segment before the quote to 
				// the buffer
				buf.append(start, str - start);

				// we're now in a quoted section
				inQuote = true;

				// skip the quote and continue from here
				start = ++str;
				continue;
			}
		}

		// nothing special - skip this character
		++str;
	}
}


int CSVFile::CreateRow()
{
	// add a row at the end of the vector
	rows.emplace_back();

	// return the row number
	return (int)(rows.size() - 1);
}

const TCHAR *CSVFile::Column::Get(int rowIndex, const TCHAR *defaultVal) const
{
	// get the value from the field, if it exists; otherwise use the default
	if (Field *field = GetField(rowIndex); field != nullptr)
		return field->Get(defaultVal);
	else
		return defaultVal;
}

int CSVFile::Column::GetInt(int rowIndex, int defaultVal) const
{
	if (const TCHAR *val = Get(rowIndex, nullptr); val != nullptr && val[0] != 0)
		return _ttoi(val);
	else
		return defaultVal;
}

float CSVFile::Column::GetFloat(int rowIndex, float defaultVal) const
{
	if (const TCHAR *val = Get(rowIndex, nullptr); val != nullptr && val[0] != 0)
		return _tcstof(val, nullptr);
	else
		return defaultVal;
}

bool CSVFile::Column::GetBool(int rowIndex, bool defaultVal) const
{
	if (const TCHAR *val = Get(rowIndex, nullptr); val != nullptr)
		return val[0] == 'Y' || val[0] == 'y' || _ttoi(val) != 0;
	else
		return defaultVal;
}

CSVFile::Column::ParsedData *CSVFile::Column::GetParsedData(int rowIndex) const
{
	// get the value from the field, if it exists; otherwise return null
	if (Field *field = GetField(rowIndex); field != nullptr)
		return field->GetParsedData();
	else
		return nullptr;
}

CSVFile::Field *CSVFile::Column::GetField(int rowIndex) const 
{
	// check if the row exists
	if (rowIndex < 0 || rowIndex >= (int)csv->rows.size())
		return nullptr;

	// get the row object
	Row &row = csv->rows[rowIndex];

	// check if the column exists in this row
	if (index >= (int)row.fields.size())
		return nullptr;

	// return the field value
	return &row.fields[index];
}

CSVFile::Field *CSVFile::Column::GetOrCreateField(int rowIndex) const
{
	// do nothing if the row doesn't exist
	if (rowIndex < 0 || rowIndex >= (int)csv->rows.size())
		return nullptr;

	// get the row object
	Row &row = csv->rows[rowIndex];

	// add null columns as needed
	while (index >= (int)row.fields.size())
	{
		row.fields.emplace_back(nullptr);
		csv->dirty = true;
	}

	// return the field
	return &row.fields[index];
}

void CSVFile::Column::Set(int rowIndex, const TCHAR *val) const
{
	if (Field *field = GetOrCreateField(rowIndex); field != nullptr)
	{
		// store the new value
		field->Set(val);

		// mark the in-memory database as updated
		csv->dirty = true;
	}
}

void CSVFile::Column::SetParsedData(int rowIndex, ParsedData *data) const
{
	if (Field *field = GetOrCreateField(rowIndex); field != nullptr)
	{
		field->SetParsedData(data);
		csv->dirty = true;
	}
}

void CSVFile::Column::Set(int rowIndex, int val) const
{
	TCHAR buf[40];
	_itot_s(val, buf, 10);
	Set(rowIndex, buf);
}

void CSVFile::Column::Set(int rowIndex, float val) const
{
	TCHAR buf[40];
	_stprintf_s(buf, _T("%f"), val);
	Set(rowIndex, buf);
}

void CSVFile::Column::SetBool(int rowIndex, bool val) const
{
	Set(rowIndex, val ? _T("Yes") : _T("No"));
}
