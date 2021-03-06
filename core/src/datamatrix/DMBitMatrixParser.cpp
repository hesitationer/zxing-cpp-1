/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "datamatrix/DMBitMatrixParser.h"
#include "datamatrix/DMVersion.h"
#include "BitMatrix.h"
#include "DecodeStatus.h"
#include "ByteArray.h"

namespace ZXing {
namespace DataMatrix {

//private final BitMatrix mappingBitMatrix;
//private final BitMatrix readMappingMatrix;
//private final Version version;

const Version*
BitMatrixParser::ReadVersion(const BitMatrix& bits)
{
	return Version::VersionForDimensions(bits.height(), bits.width());
}

/**
* <p>Extracts the data region from a {@link BitMatrix} that contains
* alignment patterns.</p>
*
* @param bitMatrix Original {@link BitMatrix} with alignment patterns
* @return BitMatrix that has the alignment patterns removed
*/
static BitMatrix ExtractDataRegion(const Version& version, const BitMatrix& bitMatrix)
{
	int symbolSizeRows = version.symbolSizeRows();
	int symbolSizeColumns = version.symbolSizeColumns();

	if (bitMatrix.height() != symbolSizeRows) {
		throw std::invalid_argument("Dimension of bitMarix must match the version size");
	}

	int dataRegionSizeRows = version.dataRegionSizeRows();
	int dataRegionSizeColumns = version.dataRegionSizeColumns();

	int numDataRegionsRow = symbolSizeRows / dataRegionSizeRows;
	int numDataRegionsColumn = symbolSizeColumns / dataRegionSizeColumns;

	int sizeDataRegionRow = numDataRegionsRow * dataRegionSizeRows;
	int sizeDataRegionColumn = numDataRegionsColumn * dataRegionSizeColumns;

	BitMatrix result(sizeDataRegionColumn, sizeDataRegionRow);
	for (int dataRegionRow = 0; dataRegionRow < numDataRegionsRow; ++dataRegionRow) {
		int dataRegionRowOffset = dataRegionRow * dataRegionSizeRows;
		for (int dataRegionColumn = 0; dataRegionColumn < numDataRegionsColumn; ++dataRegionColumn) {
			int dataRegionColumnOffset = dataRegionColumn * dataRegionSizeColumns;
			for (int i = 0; i < dataRegionSizeRows; ++i) {
				int readRowOffset = dataRegionRow * (dataRegionSizeRows + 2) + 1 + i;
				int writeRowOffset = dataRegionRowOffset + i;
				for (int j = 0; j < dataRegionSizeColumns; ++j) {
					int readColumnOffset = dataRegionColumn * (dataRegionSizeColumns + 2) + 1 + j;
					if (bitMatrix.get(readColumnOffset, readRowOffset)) {
						int writeColumnOffset = dataRegionColumnOffset + j;
						result.set(writeColumnOffset, writeRowOffset);
					}
				}
			}
		}
	}
	return result;
}

class CodewordReadHelper
{
	const BitMatrix& mappingBitMatrix;
	BitMatrix& readMappingMatrix;

public:
	CodewordReadHelper(const BitMatrix& mapping, BitMatrix& readMapping) : mappingBitMatrix(mapping), readMappingMatrix(readMapping) {}

	/**
	* <p>Reads a bit of the mapping matrix accounting for boundary wrapping.</p>
	*
	* @param row Row to read in the mapping matrix
	* @param column Column to read in the mapping matrix
	* @param numRows Number of rows in the mapping matrix
	* @param numColumns Number of columns in the mapping matrix
	* @return value of the given bit in the mapping matrix
	*/
	bool readModule(int row, int column, int numRows, int numColumns) {
		// Adjust the row and column indices based on boundary wrapping
		if (row < 0) {
			row += numRows;
			column += 4 - ((numRows + 4) & 0x07);
		}
		if (column < 0) {
			column += numColumns;
			row += 4 - ((numColumns + 4) & 0x07);
		}
		readMappingMatrix.set(column, row);
		return mappingBitMatrix.get(column, row);
	}

	/**
	* <p>Reads the 8 bits of the standard Utah-shaped pattern.</p>
	*
	* <p>See ISO 16022:2006, 5.8.1 Figure 6</p>
	*
	* @param row Current row in the mapping matrix, anchored at the 8th bit (LSB) of the pattern
	* @param column Current column in the mapping matrix, anchored at the 8th bit (LSB) of the pattern
	* @param numRows Number of rows in the mapping matrix
	* @param numColumns Number of columns in the mapping matrix
	* @return byte from the utah shape
	*/
	int readUtah(int row, int column, int numRows, int numColumns) {
		int currentByte = 0;
		if (readModule(row - 2, column - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row - 2, column - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row - 1, column - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row - 1, column - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row - 1, column, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row, column - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row, column - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(row, column, numRows, numColumns)) {
			currentByte |= 1;
		}
		return currentByte;
	}

	/**
	* <p>Reads the 8 bits of the special corner condition 1.</p>
	*
	* <p>See ISO 16022:2006, Figure F.3</p>
	*
	* @param numRows Number of rows in the mapping matrix
	* @param numColumns Number of columns in the mapping matrix
	* @return byte from the Corner condition 1
	*/
	int readCorner1(int numRows, int numColumns) {
		int currentByte = 0;
		if (readModule(numRows - 1, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 1, 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 1, 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(1, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(2, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(3, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		return currentByte;
	}

	/**
	* <p>Reads the 8 bits of the special corner condition 2.</p>
	*
	* <p>See ISO 16022:2006, Figure F.4</p>
	*
	* @param numRows Number of rows in the mapping matrix
	* @param numColumns Number of columns in the mapping matrix
	* @return byte from the Corner condition 2
	*/
	int readCorner2(int numRows, int numColumns) {
		int currentByte = 0;
		if (readModule(numRows - 3, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 2, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 1, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 4, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 3, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(1, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		return currentByte;
	}

	/**
	* <p>Reads the 8 bits of the special corner condition 3.</p>
	*
	* <p>See ISO 16022:2006, Figure F.5</p>
	*
	* @param numRows Number of rows in the mapping matrix
	* @param numColumns Number of columns in the mapping matrix
	* @return byte from the Corner condition 3
	*/
	int readCorner3(int numRows, int numColumns) {
		int currentByte = 0;
		if (readModule(numRows - 1, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 1, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 3, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(1, numColumns - 3, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(1, numColumns - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(1, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		return currentByte;
	}

	/**
	* <p>Reads the 8 bits of the special corner condition 4.</p>
	*
	* <p>See ISO 16022:2006, Figure F.6</p>
	*
	* @param numRows Number of rows in the mapping matrix
	* @param numColumns Number of columns in the mapping matrix
	* @return byte from the Corner condition 4
	*/
	int readCorner4(int numRows, int numColumns) {
		int currentByte = 0;
		if (readModule(numRows - 3, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 2, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(numRows - 1, 0, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 2, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(0, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(1, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(2, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		currentByte <<= 1;
		if (readModule(3, numColumns - 1, numRows, numColumns)) {
			currentByte |= 1;
		}
		return currentByte;
	}
};


/**
* <p>Reads the bits in the {@link BitMatrix} representing the mapping matrix (No alignment patterns)
* in the correct order in order to reconstitute the codewords bytes contained within the
* Data Matrix Code.</p>
*
* @return bytes encoded within the Data Matrix Code
* @throws FormatException if the exact number of bytes expected is not read
*/
ByteArray
BitMatrixParser::ReadCodewords(const BitMatrix& bits)
{
	const Version* version = ReadVersion(bits);
	if (version == nullptr) {
		return {};
	}

	BitMatrix mappingBitMatrix = ExtractDataRegion(*version, bits);
	BitMatrix readMappingMatrix(mappingBitMatrix.width(), mappingBitMatrix.height());

	ByteArray result(version->totalCodewords());
	int resultOffset = 0;

	int row = 4;
	int column = 0;

	int numRows = mappingBitMatrix.height();
	int numColumns = mappingBitMatrix.width();

	bool corner1Read = false;
	bool corner2Read = false;
	bool corner3Read = false;
	bool corner4Read = false;

	// Read all of the codewords
	CodewordReadHelper helper(mappingBitMatrix, readMappingMatrix);
	do {
		// Check the four corner cases
		if ((row == numRows) && (column == 0) && !corner1Read) {
			result[resultOffset++] = (uint8_t)helper.readCorner1(numRows, numColumns);
			row -= 2;
			column += 2;
			corner1Read = true;
		}
		else if ((row == numRows - 2) && (column == 0) && ((numColumns & 0x03) != 0) && !corner2Read) {
			result[resultOffset++] = (uint8_t)helper.readCorner2(numRows, numColumns);
			row -= 2;
			column += 2;
			corner2Read = true;
		}
		else if ((row == numRows + 4) && (column == 2) && ((numColumns & 0x07) == 0) && !corner3Read) {
			result[resultOffset++] = (uint8_t)helper.readCorner3(numRows, numColumns);
			row -= 2;
			column += 2;
			corner3Read = true;
		}
		else if ((row == numRows - 2) && (column == 0) && ((numColumns & 0x07) == 4) && !corner4Read) {
			result[resultOffset++] = (uint8_t)helper.readCorner4(numRows, numColumns);
			row -= 2;
			column += 2;
			corner4Read = true;
		}
		else {
			// Sweep upward diagonally to the right
			do {
				if ((row < numRows) && (column >= 0) && !readMappingMatrix.get(column, row)) {
					result[resultOffset++] = (uint8_t)helper.readUtah(row, column, numRows, numColumns);
				}
				row -= 2;
				column += 2;
			} while ((row >= 0) && (column < numColumns));
			row += 1;
			column += 3;

			// Sweep downward diagonally to the left
			do {
				if ((row >= 0) && (column < numColumns) && !readMappingMatrix.get(column, row)) {
					result[resultOffset++] = (uint8_t)helper.readUtah(row, column, numRows, numColumns);
				}
				row += 2;
				column -= 2;
			} while ((row < numRows) && (column >= 0));
			row += 3;
			column += 1;
		}
	} while ((row < numRows) || (column < numColumns));

	if (resultOffset != version->totalCodewords())
		return {};

	return result;
}

} // DataMatrix
} // ZXing
