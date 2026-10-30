/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file records.h
 * @brief The player's records: best race and lap per mode and engine, races, wins.
 *
 * Best race and best lap per mode and engine, races run, wins. The rules
 * are pure; records_load/save are the SDK.
 *
 * The saved blob carries a version: it only grows at the end, under a new
 * RECORDS_VERSION (Kodegotchi's rule for pet_t).
 */
#ifndef RECORDS_H_
#define RECORDS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDS_VERSION 2
#define REC_CIRCUITS 4    /**< room for circuits to come */
#define REC_MODES 2
#define REC_CCS   3

typedef struct {
    uint16_t version;
    uint16_t races, wins, podiums;
    uint32_t best_race_cs[REC_CIRCUITS][REC_MODES][REC_CCS];   /**< centiseconds; 0 = none yet */
    uint32_t best_lap_cs[REC_CIRCUITS][REC_MODES][REC_CCS];
} records_t;

/** @brief Version 1, saved before there was a second circuit: its records are Kody Circuit's. */
typedef struct {
    uint16_t version;
    uint16_t races, wins, podiums;
    uint32_t best_race_cs[REC_MODES][REC_CCS];
    uint32_t best_lap_cs[REC_MODES][REC_CCS];
} records_v1_t;

/**
 * @brief Carry a version 1 record over into @p r.
 * @param r The records to fill: fresh, then the old counts and times as circuit 0's.
 * @param v1 The version 1 record read from storage.
 */
void records_from_v1(records_t *r, const records_v1_t *v1);

enum { REC_NEW_RACE = 1u << 0, REC_NEW_LAP = 1u << 1 };

/**
 * @brief Fresh records: all zero, at RECORDS_VERSION.
 * @param r The records to clear.
 */
void records_init(records_t *r);

/**
 * @brief A finished race: count it, keep what beat the record.
 * @param r The records to update.
 * @param circuit The circuit raced, 0 to REC_CIRCUITS - 1.
 * @param mode The record mode, 0 to REC_MODES - 1.
 * @param cc The engine, 0 to REC_CCS - 1.
 * @param race_s The race time in seconds; 0 or less is not a time.
 * @param best_lap_s The race's best lap in seconds; 0 or less is not a time.
 * @param place Where the player finished, 1 first.
 * @param racers How many raced; wins and podiums need rivals.
 * @return REC_NEW_* bits; 0 as well when circuit, mode or cc is out of range.
 */
uint32_t records_add(records_t *r, int circuit, int mode, int cc, float race_s, float best_lap_s, int place,
                     int racers);

/**
 * @brief Read the saved records into @p r (fresh ones when nothing usable is saved).
 * @param r Where the records land.
 */
void records_load(records_t *r);

/**
 * @brief Save @p r.
 * @param r The records to save.
 * @return false when the write failed.
 */
bool records_save(const records_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RECORDS_H_ */
