/**
 * \file include/libfds/converters.h
 * \author Lukas Hutak <lukas.hutak@cesnet.cz>
 * \brief Conversion functions for IPFIX data types
 * \date 2016-2018
 */
/*
 * Copyright (C) 2016-2018 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is``, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#ifndef LIBFDS_CONVERTERS_H
#define LIBFDS_CONVERTERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>    // size_t
#include <stdint.h>    // uintXX_t, intXX_t
#include <stdbool.h>   // bool
#include <time.h>      // struct timespec
#include <string.h>    // memcpy
#include <float.h>     // float min/max
#include <assert.h>    // static_assert
#include <arpa/inet.h> // inet_ntop
#include <endian.h>    // htobe64, be64toh

#include "iemgr.h"
#include <libfds/api.h>
#include <libfds/drec.h>

/**
 * \defgroup fds_converters Data conversion
 * \ingroup publicAPIs
 * \brief Conversion "from" and "to" data types used in IPFIX messages
 * \remark Based on RFC 7011, Section 6.
 * (see https://tools.ietf.org/html/rfc7011#section-6)
 *
 * @{
 */

// Check byte order
#if (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__ \
        && __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__)
#error Unsupported endianness of the machine.
#endif

// Check the size of double and float, this MUST be here!!!
_Static_assert(sizeof(double) == sizeof(uint64_t), "Double is not 8 bytes long!");
_Static_assert(sizeof(float)  == sizeof(uint32_t), "Float is not 4 bytes long!");

/**
 * \def FDS_CONVERT_EPOCHS_DIFF
 * \brief Time difference between NTP and UNIX epoch in seconds
 *
 * NTP epoch (1 January 1900, 00:00h) vs. UNIX epoch (1 January 1970 00:00h)
 * i.e. ((70 years * 365 days) + 17 leap-years) * 86400 seconds per day
 */
#define FDS_CONVERT_EPOCHS_DIFF (2208988800ULL)

/**
 * \defgroup fds_setters Value setters
 * \ingroup fds_converters
 * \brief Convert and write a value to a field of an IPFIX record
 * @{
 */

/**
 * \brief Set a value of an unsigned integer (in big endian order a.k.a.
 *   network byte order)
 *
 * The \p value is converted from host byte order to the appropriate byte
 * order and stored to a data \p field.
 * \param[out] field  Pointer to the data field
 * \param[in]  size   Size of the data field (min: 1 byte, max: 8 bytes)
 * \param[in]  value  New value
 * \return On success returns #FDS_OK. When the \p value cannot fit
 *   in the \p field of the defined \p size, stores a saturated value and
 *   returns the value #FDS_ERR_TRUNC. When the \p size is out of
 *   range, a value of the \p field is unchanged and returns
 *   #FDS_ERR_ARG.
 */
static inline int
fds_set_uint_be(void *field, size_t size, uint64_t value)
{
    switch (size) {
    case 8:
        *((uint64_t *) field) = htobe64(value);
        return FDS_OK;

    case 4:
        if (value > UINT32_MAX) {
            *((uint32_t *) field) = UINT32_MAX; // byte conversion not required
            return FDS_ERR_TRUNC;
        }

        *((uint32_t *) field) = htonl((uint32_t) value);
        return FDS_OK;

    case 2:
        if (value > UINT16_MAX) {
            *((uint16_t *) field) = UINT16_MAX; // byte conversion not required
            return FDS_ERR_TRUNC;
        }

        *((uint16_t *) field) = htons((uint16_t) value);
        return FDS_OK;

    case 1:
        if (value > UINT8_MAX) {
            *((uint8_t *) field) = UINT8_MAX;
            return FDS_ERR_TRUNC;
        }

        *((uint8_t *) field) = (uint8_t) value;
        return FDS_OK;

    default:
        // Other sizes (3,5,6,7)
        break;
    }

    if (size == 0 || size > 8U) {
        return FDS_ERR_ARG;
    }

    const uint64_t over_limit = 1ULL << (size * 8U);
    if (value >= over_limit) {
        value = UINT64_MAX; // byte conversion not required (all bits set)
        memcpy(field, &value, size);
        return FDS_ERR_TRUNC;
    }

    value = htobe64(value);
    memcpy(field, &(((uint8_t *) &value)[8U - size]), size);
    return FDS_OK;
}

/**
 * \brief Set a value of a signed integer (in big endian order a.k.a.
 *   network byte order)
 *
 * The \p value is converted from host byte order to the appropriate byte
 * order and stored to a data \p field.
 * \param[out] field  Pointer to the data field
 * \param[in]  size   Size of the data field (min: 1 byte, max: 8 bytes)
 * \param[in]  value  New value
 * \return On success returns #FDS_OK. When the \p value cannot fit
 *   in the \p field of the defined \p size, stores a saturated value and
 *   returns the value #FDS_ERR_TRUNC. When the \p size is out of
 *   range, a value of the \p field is unchanged and returns
 *   #FDS_ERR_ARG.
 */
static inline int
fds_set_int_be(void *field, size_t size, int64_t value)
{
    switch (size) {
    case 8:
        *((int64_t *) field) = (int64_t) htobe64((uint64_t) value);
        return FDS_OK;

    case 4:
        if (value > INT32_MAX) {
            *((int32_t *) field) = (int32_t) htonl((int32_t) INT32_MAX);
            return FDS_ERR_TRUNC;
        }

        if (value < INT32_MIN) {
            *((int32_t *) field) = (int32_t) htonl((int32_t) INT32_MIN);
            return FDS_ERR_TRUNC;
        }

        *((int32_t *) field) = (int32_t) htonl((int32_t) value);
        return FDS_OK;

    case 2:
        if (value > INT16_MAX) {
            *((int16_t *) field) = (int16_t) htons((int16_t) INT16_MAX);
            return FDS_ERR_TRUNC;
        }

        if (value < INT16_MIN) {
            *((int16_t *) field) = (int16_t) htons((int16_t) INT16_MIN);
            return FDS_ERR_TRUNC;
        }

        *((int16_t *) field) = (int16_t) htons((int16_t) value);
        return FDS_OK;

    case 1:
        if (value > INT8_MAX) {
            *((int8_t *) field) = INT8_MAX;
            return FDS_ERR_TRUNC;
        }

        if (value < INT8_MIN) {
            *((int8_t *) field) = INT8_MIN;
            return FDS_ERR_TRUNC;
        }

        *((int8_t *) field) = (int8_t) value;
        return FDS_OK;

    default:
        // Other sizes (3,5,6,7)
        break;
    }

    if (size == 0 || size > 8U) {
        return FDS_ERR_ARG;
    }

    const int64_t over_limit = (((int64_t) INT64_MAX) >> ((8U - size) * 8U));
    bool over = false;

    if (value > over_limit) {
        value = (int64_t) htobe64(over_limit);
        over = true;
    } else if (value < ~over_limit) {
        value = (int64_t) htobe64(~over_limit);
        over = true;
    } else {
        value = (int64_t) htobe64(value);
    }

    memcpy(field, &(((int8_t *) &value)[8U - size]), size);
    return over ? FDS_ERR_TRUNC : FDS_OK;
}

/**
 * \brief Set a value of a float/double (in big endian order a.k.a.
 *   network byte order)
 *
 * The \p value is converted from host byte order to the appropriate byte
 * order and stored to a data \p field.
 * \note Please, keep on mind that float (4 bytes) and double (8 bytes) have
 *   different numeric precision.
 * \param[out] field  Pointer to the data field
 * \param[in]  size   Size of tha data field (4 or 8 bytes)
 * \param[in]  value  New value
 * \return On success returns #FDS_OK. When the \p value cannot fit
 *   in the \p field of the defined \p size, stores a saturated value and
 *   returns the value #FDS_ERR_TRUNC. When the \p size of the field
 *   is not valid, returns #FDS_ERR_ARG and an original value of
 *   the \p field is unchanged.
 */
FDS_API int
fds_set_float_be(void *field, size_t size, double value);

/**
 * \brief Set a value of a low precision timestamp (in big endian order a.k.a.
 *   network byte order)
 *
 * The result stored to a \p field will be converted to the appropriate byte
 * order.
 * \param[out] field  Pointer to a data field
 * \param[in]  size   Size of the data field
 * \param[in]  type   Type of the timestamp (see the remark)
 * \param[in]  value  Number of milliseconds since the UNIX epoch
 * \remark The parameter \p type can be only one of the following types:
 *   #FDS_ET_DATE_TIME_SECONDS, #FDS_ET_DATE_TIME_MILLISECONDS,
 *   #FDS_ET_DATE_TIME_MICROSECONDS, #FDS_ET_DATE_TIME_NANOSECONDS
 * \warning The \p size of the \p field MUST be 4 bytes
 *   (#FDS_ET_DATE_TIME_SECONDS) or 8 bytes (#FDS_ET_DATE_TIME_MILLISECONDS,
 *   #FDS_ET_DATE_TIME_MICROSECONDS, #FDS_ET_DATE_TIME_NANOSECONDS)
 * \warning Wraparound for dates after 8 February 2036 is not implemented.
 * \return On success returns #FDS_OK. Otherwise (usually
 *   incorrect \p size of the field) returns #FDS_ERR_ARG and an
 *   original value of the \p field is unchanged.
 */
static inline int
fds_set_datetime_lp_be(void *field, size_t size, enum fds_iemgr_element_type type,
    uint64_t value)
{
    // One second to milliseconds
    const uint64_t S1E3 = 1000ULL;

    if ((size != sizeof(uint64_t) || type == FDS_ET_DATE_TIME_SECONDS)
            && (size != sizeof(uint32_t) || type != FDS_ET_DATE_TIME_SECONDS)) {
        return FDS_ERR_ARG;
    }

    switch (type) {
    case FDS_ET_DATE_TIME_SECONDS:
        *(uint32_t *) field = htonl(value / S1E3); // To seconds
        return FDS_OK;

    case FDS_ET_DATE_TIME_MILLISECONDS:
        *(uint64_t *) field = htobe64(value);
        return FDS_OK;

    case FDS_ET_DATE_TIME_MICROSECONDS:
    case FDS_ET_DATE_TIME_NANOSECONDS: {
        // Conversion from UNIX timestamp to NTP 64bit timestamp
        uint32_t (*part)[2] = (uint32_t (*)[2]) field;

        // Seconds
        (*part)[0] = htonl((value / S1E3) + FDS_CONVERT_EPOCHS_DIFF);

        // FIXME: use precalculated values (see nemea/unirec -> ur_time.h)

        /*
         * Fraction of second (1 / 2^32)
         * The "value" uses 1/1000 sec as unit of subsecond fractions and NTP
         * uses 1/(2^32) sec as its unit of fractional time.
         * Conversion is easy: First, multiply by 2^32, then divide by 1e3.
         * Warning: Calculation must use 64bit variable!!!
         */
        uint32_t fraction = ((value % S1E3) << 32) / S1E3;
        if (type == FDS_ET_DATE_TIME_MICROSECONDS) {
            fraction &= 0xFFFFF800UL; // Make sure that last 11 bits are zeros
        }

        (*part)[1] = htonl(fraction);
        }

        return FDS_OK;
    default:
        return FDS_ERR_ARG;
    }
}

/**
 * \brief Set a value of a high precision timestamp (in big endian order a.k.a.
 *   network byte order)
 *
 * The result stored to a \p field will be converted to the appropriate byte
 * order.
 * \param[out] field  Pointer to a data field
 * \param[in]  size   Size of the data field
 * \param[in]  type   Type of the timestamp (see fds_set_datetime_lp_be())
 * \param[in]  ts     Number of seconds and nanoseconds (see time.h)
 * \warning Wraparound for dates after 8 February 2036 is not implemented.
 * \warning The value of the nanoseconds field MUST be in the range 0
 *   to 999999999. Otherwise behavior is undefined.
 * \return On success returns #FDS_OK. Otherwise (usually
 *   incorrect \p size of the field) returns #FDS_ERR_ARG and an
 *   original value of the \p field is unchanged.
 */
static inline int
fds_set_datetime_hp_be(void *field, size_t size, enum fds_iemgr_element_type type,
    struct timespec ts)
{
    const uint64_t S1E3 = 1000ULL;       // One second to milliseconds
    const uint64_t S1E6 = 1000000ULL;    // One second to microseconds
    const uint64_t S1E9 = 1000000000ULL; // One second to nanoseconds

    if ((size != sizeof(uint64_t) || type == FDS_ET_DATE_TIME_SECONDS)
            && (size != sizeof(uint32_t) || type != FDS_ET_DATE_TIME_SECONDS)) {
        return FDS_ERR_ARG;
    }

    switch (type) {
    case FDS_ET_DATE_TIME_SECONDS:
        *(uint32_t *) field = htonl(ts.tv_sec); // To seconds
        return FDS_OK;

    case FDS_ET_DATE_TIME_MILLISECONDS:
        *(uint64_t *) field = htobe64((ts.tv_sec * S1E3) + (ts.tv_nsec / S1E6));
        return FDS_OK;

    case FDS_ET_DATE_TIME_MICROSECONDS:
    case FDS_ET_DATE_TIME_NANOSECONDS: {
        // Conversion from UNIX timestamp to NTP 64bit timestamp
        uint32_t (*parts)[2] = (uint32_t (*)[2]) field;

        // Seconds
        (*parts)[0] = htonl((uint32_t) ts.tv_sec + FDS_CONVERT_EPOCHS_DIFF);

        /*
         * Fraction of second (1 / 2^32)
         * The "ts" uses 1/1e9 sec as unit of subsecond fractions and NTP
         * uses 1/(2^32) sec as its unit of fractional time.
         * Conversion is easy: First, multiply by 2^32, then divide by 1e9.
         * Warning: Calculation must use 64bit variable!!!
         */
        uint32_t fraction = (((uint64_t) ts.tv_nsec) << 32) / S1E9;
        if (type == FDS_ET_DATE_TIME_MICROSECONDS) {
            fraction &= 0xFFFFF800UL; // Make sure that last 11 bits are zeros
        }

        (*parts)[1] = htonl(fraction);
        }

        return FDS_OK;
    default:
        return FDS_ERR_ARG;
    }
}

/**
 * \brief Set a value of a boolean
 * \param[out] field  A pointer to a data field
 * \param[in]  size   Size of the data field (MUST be always 1 byte)
 * \param[in]  value  New value
 * \note This function is byte order independent because it stores the value
 *   only to one byte.
 * \remark A size of the field is always consider as 1 byte.
 * \return On success returns #FDS_OK. Otherwise (usually
 *   incorrect \p size of the field) returns #FDS_ERR_ARG and an
 *   original value of the \p field is unchanged.
 */
static inline int
fds_set_bool(void *field, size_t size, bool value)
{
    if (size != 1U) {
        return FDS_ERR_ARG;
    }

    //According to the RFC 7011, section 6.1.5. "true" == 1 and "false" == 2
    *(uint8_t *) field = value ? 1U : 2U;
    return FDS_OK;
}

/**
 * \brief Set a value of an IP address (IPv4/IPv6)
 *
 * \note This function should be byte order independent, because it assumes that
 *   IP addresses are always stored in network byte order on all platforms as
 *   usual on Linux operating systems. Therefore, the \p value should be also
 *   in network byte order.
 * \param[out] field  Pointer to a data field
 * \param[in]  size   Size of the data field (MUST be 4 or 16 bytes!)
 * \param[in]  value  Pointer to a new value of the field
 * \warning The \p value MUST be at least \p size bytes long.
 * \return On success returns #FDS_OK. Otherwise (usually
 *   incorrect \p size of the field) returns #FDS_ERR_ARG and an
 *   original value of the \p field is unchanged.
 */
static inline int
fds_set_ip(void *field, size_t size, const void *value)
{
    if (size != 4U && size != 16U) {
        return FDS_ERR_ARG;
    }

    memcpy(field, value, size);
    return FDS_OK;
}

/**
 * \brief Set a value of a MAC address
 *
 * \note This function should be byte order independent, because it assumes that
 *   MAC addresses are always stored in network byte order on all platforms as
 *   usual on Linux operating systems. Therefore, the \p value should be also
 *   in network byte order.
 * \param[out] field  Pointer to a data field
 * \param[in]  size   Size of the data field (MUST be always 6 bytes!)
 * \param[in]  value  Pointer to a new value of the field
 * \warning The \p value is left in the original byte order
 * \warning The \p value MUST be at least \p size bytes long.
 * \return On success returns #FDS_OK. Otherwise (usually
 *   incorrect \p size of the field) returns #FDS_ERR_ARG and an
 *   original value of the \p field is unchanged.
 */
static inline int
fds_set_mac(void *field, size_t size, const void *value)
{
    if (size != 6U) {
        return FDS_ERR_ARG;
    }

    memcpy(field, value, 6U);
    return FDS_OK;
}

/**
 * \brief Set a value of an octet array
 *
 * \note This function should be independent of endianness of a host computer,
 *   because it just copy a raw content of memory. Therefore, it is up to user
 *   to make sure that value is in the appropriate order in the memory of the
 *   the host computer.
 * \param[out] field  Pointer to a data field
 * \param[in]  size   Size of the data field
 * \param[in]  value  Pointer to a new value of the field
 * \remark The function can be implemented as a wrapper over memcpy.
 * \warning The \p value is left in the original byte order
 * \warning The \p value MUST be at least \p size bytes long.
 * \return On success returns #FDS_OK. Otherwise returns
 *   #FDS_ERR_ARG.
 */
static inline int
fds_set_octet_array(void *field, size_t size, const void *value)
{
    if (size == 0U) {
        return FDS_ERR_ARG;
    }

    memcpy(field, value, size);
    return FDS_OK;
}

/**
 * \brief Set a value of a string
 *
 * \note This function should be independent of endianness of a host computer
 *   because string is always stored as an array of individual bytes.
 * \param[out] field  Pointer to a data field
 * \param[in]  size   Size of the data field
 * \param[in]  value  Pointer to a new value of the field
 * \remark The function can be implemented as a wrapper over memcpy.
 * \warning The \p value MUST be at least \p size bytes long.
 * \warning The \p value MUST be a valid UTF-8 string!
 * \return On success returns #FDS_OK. Otherwise returns
 *   #FDS_ERR_ARG.
 */
static inline int
fds_set_string(void *field, size_t size, const char *value)
{
    if (size == 0U) {
        return FDS_ERR_ARG;
    }

    memcpy(field, value, size);
    return FDS_OK;
}

/**
 * @}
 *
 * \defgroup fds_getters Value getters
 * \ingroup fds_converters
 * \brief Read and convert a value from a field of an IPFIX record
 * @{
 */

/**
 * \brief Get a value of an unsigned integer (stored in big endian order a.k.a.
 *   network byte order)
 *
 * The \p value is read from a data \p field and converted from
 * the appropriate byte order to host byte order.
 * \param[in]  field  Pointer to the data field (in "network byte order")
 * \param[in]  size   Size of the data field (min: 1 byte, max: 8 bytes)
 * \param[out] value  Pointer to a variable for the result
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually the incorrect \p size of the field) returns
 *   #FDS_ERR_ARG and the \p value is not filled.
 */
static inline int
fds_get_uint_be(const void *field, size_t size, uint64_t *value)
{
    switch (size) {
    case 8:
        *value = be64toh(*(const uint64_t *) field);
        return FDS_OK;

    case 4:
        *value = ntohl(*(const uint32_t *) field);
        return FDS_OK;

    case 2:
        *value = ntohs(*(const uint16_t *) field);
        return FDS_OK;

    case 1:
        *value = *(const uint8_t *) field;
        return FDS_OK;

    default:
        // Other sizes (3,5,6,7)
        break;
    }

    if (size == 0 || size > 8) {
        return FDS_ERR_ARG;
    }

    uint64_t new_value = 0;
    memcpy(&(((uint8_t *) &new_value)[8U - size]), field, size);

    *value = be64toh(new_value);
    return FDS_OK;
}

/**
 * \brief Get a value of a signed integer (stored in big endian order a.k.a.
 *   network byte order)
 *
 * The \p value is read from a data \p field and converted from
 * "network byte order" to host byte order.
 * \param[in]  field  Pointer to the data field (in "network byte order")
 * \param[in]  size   Size of the data field (min: 1 byte, max: 8 bytes)
 * \param[out] value  Pointer to a variable for the result
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually the incorrect \p size of the field) returns
 *   #FDS_ERR_ARG and the \p value is not filled.
 */
static inline int
fds_get_int_be(const void *field, size_t size, int64_t *value)
{
    switch (size) {
    case 8:
        *value = (int64_t) be64toh(*(const uint64_t *) field);
        return FDS_OK;

    case 4:
        *value = (int32_t) ntohl(*(const uint32_t *) field);
        return FDS_OK;

    case 2:
        *value = (int16_t) ntohs(*(const uint16_t *) field);
        return FDS_OK;

    case 1:
        *value = *(const int8_t *) field;
        return FDS_OK;

    default:
        // Other sizes (3,5,6,7)
        break;
    }

    if (size == 0U || size > 8U) {
        return FDS_ERR_ARG;
    }

    /*
     * Sign extension
     * The value is in network-byte-order therefore first bit determines
     * the sign of the number. If first bit is set, prepare a new value with
     * all bits set (-1). Otherwise with all bits unset (0).
     */
    int64_t new_value = ((*(const uint8_t *) field) & 0x80) ? (-1) : 0;
    memcpy(&(((int8_t *) &new_value)[8U - size]), field, size);

    *value = be64toh(new_value);
    return FDS_OK;
}

/**
 * \brief Get a value of a float/double (stored in big endian order a.k.a.
 *   network byte order)
 *
 * The \p value is read from a data \p field and converted from
 * "network byte order" to host byte order.
 * \param[in]  field  Pointer to the data field (in "network byte order")
 * \param[in]  size   Size of the data field (4 or 8 bytes)
 * \param[out] value  Pointer to a variable for the result
 * \return On success returns #FDS_OK and fills the \p value. Otherwise
 *   (usually the incorrect \p size of the field) returns #FDS_ERR_ARG
 *   and the \p value is not filled.
 */
static inline int
fds_get_float_be(const void *field, size_t size, double *value)
{
    if (size == sizeof(uint64_t)) {
        // 64bit, we have static assert for sizeof(double) == sizeof(uint64_t)
        union {
            uint64_t u64;
            double   dbl;
        } cast_helper;

        cast_helper.u64 = be64toh(*(const uint64_t *) field);
        *value = cast_helper.dbl;
        return FDS_OK;

    } else if (size == sizeof(uint32_t)) {
        // 32bit, we have static assert for sizeof(float) == sizeof(uint32_t)
        union {
            uint32_t u32;
            float    flt;
        } cast_helper;

        cast_helper.u32 = ntohl(*(const uint32_t *) field);
        *value = cast_helper.flt;
        return FDS_OK;

    } else {
        return FDS_ERR_ARG;
    }
}

/**
 * \brief Get a value of a low precision timestamp (stored in big endian order
 *   a.k.a. network byte order)
 *
 * The \p value is read from a data \p field, converted from
 * "network byte order" to host byte order and transformed to a corresponding
 * data type.
 * \param[in]  field  Pointer to the data field (in "network byte order")
 * \param[in]  size   Size of the data field (in bytes)
 * \param[in]  type   Type of the timestamp (see the remark)
 * \param[out] value  Pointer to a variable for the result (Number of
 *   milliseconds since the UNIX epoch)
 * \remark The parameter \p type can be only one of the following types:
 *   #FDS_ET_DATE_TIME_SECONDS, #FDS_ET_DATE_TIME_MILLISECONDS,
 *   #FDS_ET_DATE_TIME_MICROSECONDS, #FDS_ET_DATE_TIME_NANOSECONDS
 * \warning The \p size of the \p field MUST be 4 bytes
 *   (#FDS_ET_DATE_TIME_SECONDS) or 8 bytes (#FDS_ET_DATE_TIME_MILLISECONDS,
 *   #FDS_ET_DATE_TIME_MICROSECONDS, #FDS_ET_DATE_TIME_NANOSECONDS)
 * \warning Wraparound for dates after 8 February 2036 is not implemented.
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually the incorrect \p size of the field) returns
 *   #FDS_ERR_ARG and the \p value is not filled.
 */
static inline int
fds_get_datetime_lp_be(const void *field, size_t size, enum fds_iemgr_element_type type,
    uint64_t *value)
{
    // One second to milliseconds
    const uint64_t S1E3 = 1000ULL;

    if ((size != sizeof(uint64_t) || type == FDS_ET_DATE_TIME_SECONDS)
            && (size != sizeof(uint32_t) || type != FDS_ET_DATE_TIME_SECONDS)) {
        return FDS_ERR_ARG;
    }

    switch (type) {
    case FDS_ET_DATE_TIME_SECONDS:
        *value = ntohl(*(const uint32_t *) field) * S1E3;
        return FDS_OK;

    case FDS_ET_DATE_TIME_MILLISECONDS:
        *value = be64toh(*(const uint64_t *) field);
        return FDS_OK;

    case FDS_ET_DATE_TIME_MICROSECONDS:
    case FDS_ET_DATE_TIME_NANOSECONDS: {
        // Conversion from NTP 64bit timestamp to UNIX timestamp
        const uint32_t (*parts)[2] = (const uint32_t (*)[2]) field;
        uint64_t result;

        // Seconds
        result = (ntohl((*parts)[0]) - FDS_CONVERT_EPOCHS_DIFF) * S1E3;

        /*
         * Fraction of second (1 / 2^32)
         * The "value" uses 1/1000 sec as unit of subsecond fractions and NTP
         * uses 1/(2^32) sec as its unit of fractional time.
         * Conversion is easy: First, multiply by 1e3, then divide by 2^32.
         * Warning: Calculation must use 64bit variable!!!
         */
        uint64_t fraction = ntohl((*parts)[1]);
        if (type == FDS_ET_DATE_TIME_MICROSECONDS) {
            fraction &= 0xFFFFF800UL; // Make sure that last 11 bits are zeros
        }

        result += (fraction * S1E3) >> 32;
        *value = result;
        }

        return FDS_OK;
    default:
        return FDS_ERR_ARG;
    }
}

/**
 * \brief Get a value of a high precision timestamp (stored in big endian order
 *   a.k.a. network byte order)
 *
 * The \p value is read from a data \p field, converted from
 * "network byte order" to host byte order and transformed to a corresponding
 * data type.
 * \param[in]  field  Pointer to the data field (in "network byte order")
 * \param[in]  size   Size of the data field (in bytes)
 * \param[in]  type   Type of the timestamp (see fds_get_datetime_lp_be())
 * \param[out] ts     Pointer to a variable for the result (see time.h)
 * \warning Wraparound for dates after 8 February 2036 is not implemented.
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually the incorrect \p size of the field) returns
 *   #FDS_ERR_ARG and the \p value is not filled.
 */
static inline int
fds_get_datetime_hp_be(const void *field, size_t size, enum fds_iemgr_element_type type,
    struct timespec *ts)
{
    const uint64_t S1E3 = 1000ULL;       // One second to milliseconds
    const uint64_t S1E6 = 1000000ULL;    // One second to microseconds
    const uint64_t S1E9 = 1000000000ULL; // One second to nanoseconds

    if ((size != sizeof(uint64_t) || type == FDS_ET_DATE_TIME_SECONDS)
            && (size != sizeof(uint32_t) || type != FDS_ET_DATE_TIME_SECONDS)) {
        return FDS_ERR_ARG;
    }

    switch (type) {
    case FDS_ET_DATE_TIME_SECONDS:
        ts->tv_sec = ntohl(*(const uint32_t *) field);
        ts->tv_nsec = 0;
        return FDS_OK;

    case FDS_ET_DATE_TIME_MILLISECONDS: {
        const uint64_t new_value = be64toh(*(const uint64_t *) field);
        ts->tv_sec =  new_value / S1E3;
        ts->tv_nsec = (new_value % S1E3) * S1E6;
        }

        return FDS_OK;

    case FDS_ET_DATE_TIME_MICROSECONDS:
    case FDS_ET_DATE_TIME_NANOSECONDS: {
        // Conversion from NTP 64bit timestamp to UNIX timestamp
        const uint32_t (*parts)[2] = (const uint32_t (*)[2]) field;

        // Seconds
        ts->tv_sec = ntohl((*parts)[0]) - FDS_CONVERT_EPOCHS_DIFF;

        /*
         * Fraction of second (1 / 2^32)
         * The "ts" uses 1/1e9 sec as unit of subsecond fractions and NTP
         * uses 1/(2^32) sec as its unit of fractional time.
         * Conversion is easy: First, multiply by 1e9, then divide by 2^32.
         * Warning: Calculation must use 64bit variable!!!
         */
        uint64_t fraction = ntohl((*parts)[1]);
        if (type == FDS_ET_DATE_TIME_MICROSECONDS) {
            fraction &= 0xFFFFF800UL; // Make sure that last 11 bits are zeros
        }

        /*
         * Rounding error correction
         * Without correction, almost all numbers will be slightly imprecise.
         * For example, if the number 999,999,999 nanoseconds is converted to
         * a fraction and back to nanoseconds, rounding will change the number
         * to 999,999,998 (the last digit is incorrect). This can be solved
         * be rounding result in nanoseconds based on number after the decimal
         * mask. Because we don't want to work with floating point numbers
         * we perform a small trick. See below.
         */
        fraction *= S1E9;
        fraction >>= 31;      // Instead of 32, shift only 31 times
        if (fraction & 0x1) { // If the last digit is odd, increment the number
            ++fraction;
        }

        ts->tv_nsec = fraction >> 1; // Perform the last shift
        }

        return FDS_OK;
    default:
        return FDS_ERR_ARG;
    }
}

/**
 * \brief Get a value of a boolean
 *
 * \param[in]  field  Pointer to a data field
 * \param[in]  size   Size of the data field (MUST be always 1 byte)
 * \param[out] value  Pointer to a variable for the result
 * \note This function is byte order independent because it assumes the value
 *   is only one byte long.
 * \remark A size of the field is always consider as 1 byte.
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually invalid boolean value - see the definition of the
 *   boolean for IPFIX, RFC 7011, Section 6.1.5.) returns #FDS_ERR_ARG
 *   and the \p value is not filled.
 */
static inline int
fds_get_bool(const void *field, size_t size, bool *value)
{
    if (size != 1U) {
        return FDS_ERR_ARG;
    }

    switch (*(const uint8_t *) field) {
    case 1U: // True
        *value = true;
        return FDS_OK;
    case 2U: // False (according to the RFC 7011, section 6.1.5. "false" == 2)
        *value = false;
        return FDS_OK;
    default:
        return FDS_ERR_ARG;
    }
}

/**
 * \brief Get a value of an IP address (IPv4 or IPv6)
 *
 * \note This function should be byte order independent, because it assumes that
 *   IP addresses are always stored in network byte order on all platforms as
 *   usual on Linux operating systems. Therefore, the \p value should be also
 *   in network byte order.
 * The \p value is left in the original byte order (i.e. network byte order)
 * \param[in]  field  Pointer to a data field
 * \param[in]  size   Size of the data field (MUST be 4 or 16 bytes!)
 * \param[out] value  Pointer to a variable for the result
 * \warning The \p value MUST be at least \p size bytes long.
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually the incorrect \p size of the field) returns
 *   #FDS_ERR_ARG and the \p value is not filled.
 */
static inline int
fds_get_ip(const void *field, size_t size, void *value)
{
    if (size != 4U && size != 16U) {
        return FDS_ERR_ARG;
    }

    memcpy(value, field, size);
    return FDS_OK;
}

/**
 * \brief Get a value of a MAC address (IPv4 or IPv6)
 *
 * \note This function should be byte order independent, because it assumes that
 *   MAC addresses are always stored in network byte order on all platforms as
 *   usual on Linux operating systems. Therefore, the \p value should be also
 *   in network byte order.
 * The \p value is left in the original byte order (i.e. network byte order)
 * \param[in]  field  Pointer to a data field
 * \param[in]  size   Size of the data field (MUST be always 6 bytes!)
 * \param[out] value  Pointer to a variable for the result
 * \warning The \p value MUST be at least \p size bytes long.
 * \return On success returns #FDS_OK and fills the \p value.
 *   Otherwise (usually the incorrect \p size of the field) returns
 *   #FDS_ERR_ARG and the \p value is not filled.
 */
static inline int
fds_get_mac(const void *field, size_t size, void *value)
{
    if (size != 6U) {
        return FDS_ERR_ARG;
    }

    memcpy(value, field, 6U);
    return FDS_OK;
}

/**
 * \brief Get a value of an octet array
 *
 * \note This function should be independent of endianness of a host computer,
 *   because it just copy a raw content of memory. Therefore, it is up to user
 *   to make sure that value is in the appropriate order in the memory of the
 *   the host computer.
 * \note The \p size of the \p field can be also zero.
 *
 * \param[in]  field  Pointer to a data field
 * \param[in]  size   Size of the data field
 * \param[out] value  Pointer to the output buffer
 * \remark Can be implemented as a wrapper over memcpy.
 * \warning The \p value MUST be at least \p size bytes long.
 * \return On success returns #FDS_OK. Otherwise returns
 *   #FDS_ERR_ARG.
 */
static inline int
fds_get_octet_array(const void *field, size_t size, void *value)
{
    memcpy(value, field, size);
    return FDS_OK;
}

/**
 * \brief Get a value of a string
 *
 * \note This function should be independent of endianness of a host computer
 *   because string is always stored as an array of individual bytes.
 * \note The \p size of the \p field can be also zero.
 *
 * \param[in]  field  Pointer to a data field
 * \param[in]  size   Size of the data field
 * \param[out] value  Pointer to the output buffer
 * \remark Can be implemented as a wrapper over memcpy.
 * \warning The \p value MUST be at least \p size bytes long.
 * \warning The terminating null byte is not added!
 * \return On success returns #FDS_OK. Otherwise returns
 *   #FDS_ERR_ARG.
 */
static inline int
fds_get_string(const void *field, size_t size, char *value)
{
    memcpy(value, field, size);
    return FDS_OK;
}

/**
 * @}
 *
 * \defgroup fds_to_string To string
 * \brief Read and convert a value from a field of an IPFIX record to
 * a character string
 *
 * \note Output format of all functions conforms to RFC 7373
 *
 * @{
 */

/**
 * \def FDS_CONVERT_STRLEN_INT
 * \brief Minimal size of na output buffer for any signed and unsigned integer conversion
 * \note Unsigned numbers: 20 numbers + 1x '\0'
 * \note Signed numbers: 1 sign character + 19 numbers + 1x '\0'
 */
#define FDS_CONVERT_STRLEN_INT (21)

/**
 * \def FDS_CONVERT_STRLEN_MAC
 * \brief Minimal size of an output buffer for any MAC address conversion
 * \note 2 * 6 groups + 5 colons + 1 * '\0'
 */
#define FDS_CONVERT_STRLEN_MAC (18)

/**
 * \def FDS_CONVERT_STRLEN_IP
 * \brief Minimal size of an output buffer for any IP address conversion
 * \note Usually 48 bytes
 */
#define FDS_CONVERT_STRLEN_IP (INET6_ADDRSTRLEN)

/**
 * \def FDS_CONVERT_STRLEN_DATE
 * \brief Minimum size of an output buffer for guaranteed time conversion
 * \note This value represents length of the longest possible string, maximum
 *   possible value is milliseconds in local time i.e.
 *   "584556019-04-03T14:25:50.000000000+0000"
 */
#define FDS_CONVERT_STRLEN_DATE (40)

/**
 * \def FDS_CONVERT_STR_TRUE
 * \brief String that is supposed to replace "true" boolean value
 */
#define FDS_CONVERT_STR_TRUE "true"

/**
 * \def FDS_CONVERT_STRLEN_TRUE
 * \brief Length of the #FDS_CONVERT_STR_TRUE string
 * \note The value includes terminating null byte i.e. '\0'
 */
#define FDS_CONVERT_STRLEN_TRUE (sizeof(FDS_CONVERT_STR_TRUE))

/**
 * \def FDS_CONVERT_STR_FALSE
 * \brief String that is supposed to replace "true" boolean value
 */
#define FDS_CONVERT_STR_FALSE "false"

/**
 * \def FDS_CONVERT_STRLEN_FALSE
 * \brief Length of the #FDS_CONVERT_STR_FALSE string
 * \note The value includes terminating null byte i.e. '\0'
 */
#define FDS_CONVERT_STRLEN_FALSE (sizeof(FDS_CONVERT_STR_FALSE))

/**
 * \def FDS_CONVERT_STRX
 * \brief Auxiliary macro for stringizing the result of expansion
 * \note Example: FDS_CONVERT_STRX(EXIT_SUCCESS) == "0"
 */
#define FDS_CONVERT_STRX(s) FDS_CONVERT_STR(s)

/**
 * \def FDS_CONVERT_STR
 * \brief Auxiliary macro for converting parameter to string
 * \note Example: FDS_CONVERT_STR(EXIT_SUCCESS) == "EXIT_SUCCESS" (C++)
 */
#define FDS_CONVERT_STR(s) #s

/**
 * \enum fds_convert_time_fmt
 * \brief Time conversion timezone and precision format
 * \note All following format options should comply with ISO 8601 standard
 *   for representation of dates and times.
 */
enum fds_convert_time_fmt {
    /** UTC time, seconds (i.e. %Y-%m-%dT%H:%M:%SZ)                          */
    FDS_CONVERT_TF_SEC_UTC = 0x01,
    /** UTC time, milliseconds (i.e. "%Y-%m-%dT%H:%M:%S.mmmZ")               */
    FDS_CONVERT_TF_MSEC_UTC = 0x02,
    /** UTC time, microseconds (i.e. "%Y-%m-%dT%H:%M:%S.uuuuuuZ")            */
    FDS_CONVERT_TF_USEC_UTC = 0x03,
    /** UTC time, nanoseconds (i.e. "%Y-%m-%dT%H:%M:%S.nnnnnnnnnZ")          */
    FDS_CONVERT_TF_NSEC_UTC = 0x04,
    /** Local time, seconds (i.e. %Y-%m-%dT%H:%M:%S±hhmm)                    */
    FDS_CONVERT_TF_SEC_LOCAL = 0x11,
    /** Local time, milliseconds (i.e. "%Y-%m-%dT%H:%M:%S.mmm±hhmm")         */
    FDS_CONVERT_TF_MSEC_LOCAL = 0x12,
    /** Local time, microseconds (i.e. "%Y-%m-%dT%H:%M:%S.uuuuuu±hhmm")      */
    FDS_CONVERT_TF_USEC_LOCAL = 0x13,
    /** Local time, nanoseconds (i.e. "%Y-%m-%dT%H:%M:%S.nnnnnnnnn±hhmm")    */
    FDS_CONVERT_TF_NSEC_LOCAL = 0x14
};

/**
 * \brief Universal conversion function (from an IPFIX field to a character
 *   string)
 *
 * Convert a value of a user defined data type (in big endian order a.k.a.
 * network byte order) to a character string. The \p value is read from
 * a data \p field and converted from the appropriate byte order and converted
 * to string. Terminating null byte ('\0') is always added to the string.
 *
 * Output format of all data types corresponds to RFC 7373. However, timestamps
 * always end with "Z" to represent UTC timezone and UTF-8 string are escaped
 * as described in fds_string2str().
 *
 * \note Structured data types (::FDS_ET_BASIC_LIST, ::FDS_ET_SUB_TEMPLATE_LIST,
 *   ::FDS_ET_SUB_TEMPLATE_MULTILIST) cannot be directly converted to string.
 *   These abstract data types, defined for IPFIX Structured Data (RFC 6313),
 *   do not represent actual data types.
 *
 * \param[in]  field    Pointer to a data field (in "network byte order")
 * \param[in]  size     Size of the data field (in bytes)
 * \param[in]  type     Data type of the \p field
 * \param[out] str      Pointer to an output character buffer
 * \param[in]  str_size Size of the output buffer (in bytes)
 * \return On success returns a number of characters (excluding the termination
 *   null byte) placed into the buffer \p str. Therefore, if the result is
 *   greater that or equal to zero, conversion was successful.
 * \return #FDS_ERR_BUFFER if the length of the result string (including the
 *   termination null byte) would exceed \p str_size. The context written to
 *   the output buffer \p str is undefined.
 * \return #FDS_ERR_FORMAT if the \p type is not supported (or structured).
 * \return #FDS_ERR_ARG otherwise (typically invalid data format).
 */
FDS_API int
fds_field2str_be(const void *field, size_t size,
    enum fds_iemgr_element_type type, char *str, size_t str_size);

/**
 * \brief Convert a value of an unsigned integer (in big endian order a.k.a.
 *   network byte order) to a character string
 *
 * The \p value is read from a data \p field and converted from
 * the appropriate byte order to host byte order and converted to string.
 * Terminating null byte ('\0') is always added to the string.
 *
 * \warning The buffer size \p str_size MUST be always greater or equal to
 *   #FDS_CONVERT_STRLEN_INT!
 * \param[in]  field     Pointer to a data field (in "network byte order")
 * \param[in]  size      Size of the data field (in bytes)
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_uint2str_be(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Convert a value of a signed integer (in big endian order a.k.a.
 *   network byte order) to a character string
 *
 * The \p value is read from a data \p field, converted from
 * the appropriate byte order to host byte order and converted to string.
 * Terminating null byte ('\0') is always added to the string.
 *
 * warning The buffer size \p str_size MUST be always greater or equal to
 *   #FDS_CONVERT_STRLEN_INT!
 * \param[in]  field     Pointer to a data field (in "network byte order")
 * \param[in]  size      Size of the data field (in bytes)
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_int2str_be(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Convert a value of a float/double (in big endian order a.k.a.
 *   network byte order) to a character string
 *
 * The \p value is read from a data \p field, converted from
 * the appropriate byte order to host byte order and converted to string.
 * \note Keep in mind that a valid output is also positive and negative
 *   infinity (i.e. "inf"/"-inf") and non a number  (i.e. "NaN")
 *
 * \param[in]  field     Pointer to the data field (in "network byte order")
 * \param[in]  size      Size of the data field (4 or 8 bytes)
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_float2str_be(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Convert a value of a timestamp (in big endian order a.k.a.
 *   network byte order) to a character string (in UTC)
 *
 * The value wil be read from a data \p field, converted from
 * the appropriate byte order to host byte order and transformed to string.
 * For example output (in milliseconds) looks like "2016-06-22T08:15:23.123"
 * (without quotation marks)
 *
 * \param[in]  field     Pointer to the data field (in "network byte order")
 * \param[in]  size      Size of the data field (in bytes)
 * \param[in]  type      Type of the timestamp (see the remark)
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \param[in]  fmt       Output format (see ::fds_convert_time_fmt)
 * \remark For more details about the parameter \p type see the documentation
 *    of fds_get_datetime_lp_be().
 * \remark The size of the output buffer (\p str_size) must be at least
 *   #FDS_CONVERT_STRLEN_DATE bytes to guarantee enough size for all conversion
 *   types.
 * \warning Wraparound for dates after 8 February 2036 is not implemented.
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_datetime2str_be(const void *field, size_t size, enum fds_iemgr_element_type type,
    char *str, size_t str_size, enum fds_convert_time_fmt fmt);

/**
 * \brief Convert a boolean value to a character string
 *
 * \param[in]  field      Pointer to a data field
 * \param[out] str        Pointer to an output character buffer
 * \param[in]  str_size   Size of the output buffer (in bytes)
 * \note This function is byte order independent because it assumes the value
 *   is only one byte long.
 * \remark Output strings are "true" and "false".
 * \remark A size of the \p field is always consider as 1 byte.
 * \remark If the content of the \p field is invalid value, the function
 *   will also return #FDS_ERR_ARG.
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_bool2str(const void *field, char *str, size_t str_size);

/**
 * \brief Convert a value of an IP address (IPv4/IPv6) to a character string
 *
 * \remark The size of the output buffer (\p str_size) should be at least
 *   #FDS_CONVERT_STRLEN_IP bytes to guarantee enough size for conversion.
 * \note This function should be byte order independent, because it assumes that
 *   IP addresses are always stored in network byte order on all platforms as
 *   usual on Linux operating systems. Therefore, the \p field should be also
 *   in network byte order.
 * \param[in]  field     Pointer to a data field
 * \param[in]  size      Size of the data field (4 or 16 bytes)
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_ip2str(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Convert a value of a MAC address to a character string
 *
 * \note This function should be byte order independent, because it assumes that
 *   MAC addresses are always stored in network byte order on all platforms as
 *   usual on Linux operating systems. Therefore, the \p value should be also
 *   in network byte order.
 * \param[in]  field     Pointer to a data field
 * \param[in]  size      Size of the data field (MUST be always 6 bytes!)
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \remark A size of the \p field is always consider as 6 byte.
 * \remark Output format is six groups of hexadecimal digits separated by colon
 *   symbols (:), for example: "00:0a:bc:e0:12:34" (without quotation marks)
 * \remark The size of the output buffer (\p str_size) must be at least
 *   #FDS_CONVERT_STRLEN_MAC bytes to guarantee enough size for conversion.
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_mac2str(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Convert a value of an octet array to a character string
 *
* \note This function should be independent of endianness of a host computer,
 *   because it just read a raw content of memory. Therefore, it is up to user
 *   to make sure that value is in the appropriate order in the memory of the
 *   the host computer.
 * \param[in]  field     Pointer to a data field
 * \param[in]  size      Size of the data field
 * \param[out] str       Pointer to an output character buffer
 * \param[in]  str_size  Size of the output buffer (in bytes)
 * \remark Output format  represents data as a string of pairs of hexadecimal
 *   digits, one pair per byte, in the order the bytes would appear on the
 *   "wire" (i.e. network byte order). Example: "HH...", where "HH" is
 *   hexadecimal representation of one byte.
 * \remark Minimum size of the output buffer (\p str_size) must be at least
 *   (2 * \p size) + 1 bytes.
 * \return Same as a return value of fds_field2str_be().
 */
FDS_API int
fds_octet_array2str(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Convert a value of an IPFIX string to an escaped UTF-8 string
 *
 * \note This function should be independent of endianness of a host computer
 *   because string is always stored as an array of individual bytes.
 * \param[in]  field     Pointer to a data field
 * \param[in]  size      Size of the data field
 * \param[out] str       Pointer to an output buffer
 * \param[in]  str_size  Size of the output buffer
 * \remark In the worst case scenario (i.e. replacing all characters with
 *   hexadecimal representation), required size of the output buffer is
 *   (4 * \p size) + 1.
 * \remark Some characters representable by escape sequences are represented as
 *   particular escape sequences i.e. alarm, backspace, formfeed, newline,
 *   carriage return, horizontal tab and vertical tab. Non-printable (e.g. other
 *   control characters) are replaced with \\xHH (where "HH" is a hexadecimal
 *   value).
 * \remark Malformed characters are replaced with UTF-8 "REPLACEMENT CHARACTER"
 * \note Backslash, single and double quotation mark characters and NOT escaped.
 * \return Same as a return value of fds_field2str_be(). A positive number
 *   represents number of written bytes. It does NOT represent number of UTF-8
 *   characters!
 */
FDS_API int
fds_string2str(const void *field, size_t size, char *str, size_t str_size);

/**
 * \brief Check encoding of a UTF-8 string
 * \note Some UTF-8 strings can be malformed and the original usage of UTF-8
 *   was open to a canonicalization exploit (see Unicode Technical Report #36)
 * \param[in] field Pointer to the UTF-8 string
 * \param[in] size  Size of the string (in bytes)
 * \return On success returns #FDS_OK. Otherwise returns #FDS_ERR_ARG.
 */
FDS_API int
fds_string_utf8check(const void *field, size_t size);


/**
 * @}
 *
 * \defgroup fds_to_json To JSON
 * \brief Read and convert a value from IPFIX Data Record to JSON
 *
 * @{
 */

/**
 * \enum fds_convert_drec2json
 * \brief Output format flags for conversion from IPFX Data Record to JSON
 */
enum fds_convert_drec2json {
    /**
     * Allow to realloc output buffer if its size is not sufficient.
     */
    FDS_CD2J_ALLOW_REALLOC   = (1U << 0),
    /**
     * In case of a Biflow record, interpret it from reverse point of view.
     * By default, it is interpreted from forward point of view.
     */
    FDS_CD2J_BIFLOW_REVERSE  = (1U << 1),
    /**
     * In case of a Biflow record, skip (i.e. ignore) all reverse fields.
     * The flag can be combined with ::FDS_CD2J_BIFLOW_REVERSE. In that case,
     * the filter is applied on the remapped fields.
     */
    FDS_CD2J_REVERSE_SKIP    = (1U << 2),
    /**
     * Skip fields with unknown definition of an Information Element.
     */
    FDS_CD2J_IGNORE_UNKNOWN  = (1U << 3),
    /**
     * Convert standard TCP flags identification (i.e. "iana:tcpControlBits")
     * to textual representation. For example, ".A..S.".
     */
    FDS_CD2J_FORMAT_TCPFLAGS = (1U << 4),
    /**
     * Convert standart protocol identification (i.e. "iana:protocolIdentifier")
     * to textual representation. For example, instead of 6 writes "TCP").
     */
    FDS_CD2J_FORMAT_PROTO    = (1U << 5),
    /**
     * Ignore non-printable characters (newline, tab, control characters, etc.)
     * in IPFIX string fields instead of escaping them.
     */
    FDS_CD2J_NON_PRINTABLE   = (1U << 6),
    /**
     * Use the format of unknown Information Elements (i.e. "enXX:idYY") for
     * names for all name-value pairs. E.g., instead of "iana:octetDeltaCount"
     * use always "en0:id1".
     */
    FDS_CD2J_NUMERIC_ID      = (1U << 7),
    /**
     * Convert all timestamps to ISO 8601 textual representation of UTC with
     * milliseconds, e.g. "2019-05-22T22:34:57.828Z"
     */
    FDS_CD2J_TS_FORMAT_MSEC  = (1U << 8),
    /**
     * Always convert IPFIX octetArray field as hexadecimal value in network
     * byte order i.e. never try to interpret is as unsigned integer.
     * For example, "0x8BADF00D".
     */
    FDS_CD2J_OCTETS_NOINT    = (1U << 9)
};

/**
 * @brief Convert IPFIX Data Record to JSON
 *
 * All fields from the Data Record are converted to JSON. Output format can be
 * changed using combination of ::fds_convert_drec2json.
 *
 * However, by default:
 *   - All fields in the Data Record are formatted as name-value pairs where
 *     the names (i.e. keys) are identification of Information Elements (IEs).
 *     Usually, names are represented as "\<scope\>:\<name\>", where \<scope\>
 *     is a name of an organization that manages the field and \<name\> is
 *     the field identification within the scope. For example.
 *     "iana:octetDeltaCount". However, if an IE definition is unknown, keys use
 *     format "enXX:idYY", where XX is an Enterprise Number and YY is
 *     an Information Element ID.
 *   - Fields with unknown definition of their Information Elements are
 *     interpreted as octetArray.
 *   - Fields with octetArray type are interpreted as unsigned integer if their
 *     size is less or equal to 8 bytes. Fields with the size above the limit
 *     are formatted as hexadecimal value in network byte order. To change this
 *     behavior, see the conversion flags.
 *   - If conversion of an IPFIX field of the Data Record fails (for example,
 *     due to invalid size or value), "null" is used as its value.
 *   - All timestamps are formatted as number of milliseconds from UNIX Epoch.
 *
 * @note
 *   If the @p buffer contains pointer to NULL, a sufficient buffer will
 *   be allocated and must be later freed by the user using free(). Size of
 *   the buffer is placed to \p str_size and can be greater than the length
 *   of result!
 * @note
 *   If ::FDS_CD2J_ALLOW_REALLOC flag is set and the output buffer size is not
 *   sufficient, the buffer is reallocated using realloc() and new size
 *   (usually greater than the length of the result) is stored to \p str_size.
 *
 * @param[in]     rec      IPFIX Data Record to convert
 * @param[in]     flags    Conversion flags (see ::fds_convert_drec2json)
 * @param[in]     ie_mgr   Information Element manager (necessary for decoding
 *                         basicLists, can be NULL)
 * @param[out]    str      Output buffer where the JSON string will be stored
 * @param[in,out] str_size Size of the output buffer
 *
 * \return On success returns a number of characters (excluding the termination
 *   null byte) placed into the buffer \p str. Therefore, if the result is
 *   greater that or equal to zero, conversion was successful (Note: the null
 *   byte has been also successfully added at the end of the JSON string)
 * \return #FDS_ERR_BUFFER if the length of the result string (including the
 *   termination null byte) would exceed \p str_size and #FDS_CD2J_ALLOW_REALLOC
 *   flag is not set. The context written to the output buffer \p str is
 *   undefined.
 * \return #FDS_ERR_NOMEM in case of memory allocation error (can happen only
 *   if #FDS_CD2J_ALLOW_REALLOC is set or \p str contains reference to NULL)
 */
FDS_API int
fds_drec2json(const struct fds_drec *rec, uint32_t flags, const fds_iemgr_t *ie_mgr, char **str,
    size_t *str_size);

#ifdef __cplusplus
}
#endif

#endif /* LIBFDS_CONVERTERS_H */

/**
 * @}
 * @}
 */
