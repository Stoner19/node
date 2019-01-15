/**
 * @file database.h
 * @author Roman Bukin, Evgeny Zalivochkin
 */

#ifndef _CREDITS_CSDB_DATABASE_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_H_INCLUDED_

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <client/params.hpp>

#include "csdb/internal/types.hpp"

namespace csdb {

class Database {
public:
  enum Error {
    NoError = 0,
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOError = 5,
    NotOpen = 6,
    UnknownError = 255,
  };

public:
  using byte_array = cs::Bytes;

protected:
  Database();

public:
  virtual ~Database();

  virtual bool is_open() const = 0;
  virtual bool put(const byte_array &key, uint32_t seq_no, const byte_array &value) = 0;
  virtual bool get(const byte_array &key, byte_array *value = nullptr) = 0;
  virtual bool get(const uint32_t seq_no, byte_array *value = nullptr) = 0;
  virtual bool remove(const byte_array &key) = 0;

  using Item = std::pair<byte_array, byte_array>;
  using ItemList = std::vector<Item>;
  virtual bool write_batch(const ItemList &items) = 0;

#ifdef TRANSACTIONS_INDEX
  virtual bool putToTransIndex(const byte_array &key, const byte_array &value) = 0;
  virtual bool getFromTransIndex(const byte_array &key, byte_array *value) = 0;
#endif

  class Iterator {
  protected:
    Iterator();

  public:
    virtual ~Iterator();

  public:
    virtual bool is_valid() const = 0;
    virtual void seek_to_first() = 0;
    virtual void seek_to_last() = 0;
    virtual void seek(const byte_array &key) = 0;
    virtual void next() = 0;
    virtual void prev() = 0;
    virtual byte_array key() const = 0;
    virtual byte_array value() const = 0;
  };
  using IteratorPtr = std::shared_ptr<Iterator>;
  virtual IteratorPtr new_iterator() = 0;

public:
  Error last_error() const;
  std::string last_error_message() const;

protected:
  void set_last_error(Error error = NoError, const std::string &message = std::string());
  void set_last_error(Error error, const char *message, ...);
};

}  // namespace csdb

#endif  // _CREDITS_CSDB_DATABASE_H_INCLUDED_
