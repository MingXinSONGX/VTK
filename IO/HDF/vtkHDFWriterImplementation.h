// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkHDFWriterImplementation
 * @brief   Implementation class for vtkHDFWriter
 *
 * Opens, closes and writes information to a VTK HDF file.
 */

#ifndef vtkHDFWriterImplementation_h
#define vtkHDFWriterImplementation_h

#include "vtkAbstractArray.h"
#include "vtkCellArray.h"
#include "vtkHDF5ScopedHandle.h"
#include "vtkHDFUtilities.h"
#include "vtkHDFWriter.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkHDFWriter::Implementation
{
public:
  hid_t GetRoot() { return this->Root; }
  hid_t GetFile() { return this->File; }
  hid_t GetStepsGroup() { return this->StepsGroup; }
  const char* GetLastError() { return this->LastError; }

  /**
   * Write version and type attributes to the root group
   * A root must be open for the operation to succeed
   * Returns wether the operation was successful
   * If the operation fails, some attributes may have been written
   */
  bool WriteHeader(const char* hdfType);

  /**
   * Open the file from the filename and create the root VTKHDF group
   * Overwrite the file if it exists by default
   * This doesn't write any attribute or dataset to the file
   * This file is not closed until another root is opened or this object is destructed
   * Returns wether the operation was successful
   * If the operation fails, the file may have been created
   */
  bool OpenRoot(bool overwrite = true);

  /**
   * Create the steps group in the root group. Set a member variable to store the group, so it can
   * be retrieved later using `GetStepsGroup` function.
   */
  bool CreateStepsGroup();

  /**
   * @struct PolyDataTopos
   * @brief Stores a group name and the corresponding cell array.
   *
   * Use this structure to avoid maintaining two arrays which is error prone (AOS instead of SOA)
   */
  typedef struct
  {
    const char* hdfGroupName;
    vtkCellArray* cellArray;
  } PolyDataTopos;

  /**
   * Get the cell array for the POLY_DATA_TOPOS
   */
  std::vector<PolyDataTopos> GetCellArraysForTopos(vtkPolyData* polydata);

  // Creation utility functions

  /**
   * Create a dataset in the given group with the given parameters and write data to it
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle CreateAndWriteHdfDataset(hid_t group, hid_t type, hid_t source_type,
    const char* name, int rank, const hsize_t dimensions[], const void* data);

  /**
   * Create a HDF dataspace
   * It is simple (not scalar or null) which means that it is an array of elements
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5SHandle CreateSimpleDataspace(int rank, const hsize_t dimensions[]);

  /**
   * Create a scalar integer attribute in the given group
   */
  vtkHDF::ScopedH5AHandle CreateScalarAttribute(hid_t group, const char* name, int value);

  /**
   * Create an unlimited HDF dataspace with a dimension of `0 * numCols`.
   * This dataspace can be attached to a chunked dataset and extended afterwards.
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5SHandle CreateUnlimitedSimpleDataspace(hsize_t numCols);

  /**
   * Create a dataset in the given group from a dataspace
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle CreateHdfDataset(
    hid_t group, const char* name, hid_t type, hid_t dataspace);

  /**
   * Create a dataset in the given group
   * It internally creates a dataspace from a rank and dimensions
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle CreateHdfDataset(
    hid_t group, const char* name, hid_t type, int rank, const hsize_t dimensions[]);

  /**
   * Create a chunked dataset in the given group from a dataspace.
   * Chunked datasets are used to append data iteratively
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle CreateChunkedHdfDataset(hid_t group, const char* name, hid_t type,
    hid_t dataspace, hsize_t numCols, hsize_t chunkSize[]);

  /**
   * Creates a dataspace to the exact array dimensions
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5SHandle CreateDataspaceFromArray(vtkAbstractArray* dataArray);

  /**
   * Creates a dataset in the given group from a dataArray and write data to it
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle CreateDatasetFromDataArray(
    hid_t group, const char* name, hid_t type, vtkAbstractArray* dataArray);

  /**
   * Creates a single-value dataset and write a value to it.
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle CreateSingleValueDataset(hid_t group, const char* name, int value);

  /**
   * Create a chunked dataset with an empty extendable dataspace using chunking.
   * Returned scoped handle may be invalid
   */
  vtkHDF::ScopedH5DHandle InitDynamicDataset(
    hid_t group, const char* name, hid_t type, hsize_t cols, hsize_t chunkSize[]);

  /**
   * Add a single value of integer type to an existing dataspace.
   * Return true if the write operation was successful.
   */
  bool AddSingleValueToDataset(hid_t dataset, int value, bool offset);

  /**
   * Append a full data array at the end of an existing infinite dataspace.
   * Return true if the write operation was successful.
   */
  bool AddArrayToDataset(hid_t dataset, vtkAbstractArray* dataArray);

  /**
   * Append the given array to the dataset with the given `name`, creating it if it does not exist
   * yet. If the dataset/dataspace already exists, array types much match.
   * Return true if the operation was successful.
   */
  bool AddOrCreateDataset(hid_t group, const char* name, hid_t type, vtkAbstractArray* dataArray);

  /**
   * Append a single integer value to the dataset with name `name` in `group` group.
   * Create the dataset and dataspace if it does not exist yet.
   * When offset is true, the value written to the dataset is offset by the previous value of the
   * dataspace.
   * Return true if the operation is successful.
   */
  bool AddOrCreateSingleValueDataset(hid_t group, const char* name, int value, bool offset = false);

  Implementation(vtkHDFWriter* writer);
  virtual ~Implementation();

private:
  vtkHDFWriter* Writer;
  const char* LastError;
  vtkHDF::ScopedH5FHandle File;
  vtkHDF::ScopedH5GHandle Root;
  vtkHDF::ScopedH5GHandle StepsGroup;
};

VTK_ABI_NAMESPACE_END
#endif
// VTK-HeaderTest-Exclude: vtkHDFWriterImplementation.h
