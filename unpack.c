/***************************************************************************
 * unpack.c:
 *
 * Generic routines to unpack miniSEED records.
 *
 * Appropriate values from the record header will be byte-swapped to
 * the host order.  The purpose of this code is to provide a portable
 * way of accessing common SEED data record header information.  All
 * data structures in SEED 2.4 data records are supported.  The data
 * samples are optionally decompressed/unpacked.
 *
 * Written by Chad Trabant,
 *   IRIS Data Management Center
 ***************************************************************************/
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libmseed.h"
#include "unpack.h"
#include "mseedformat.h"
#include "unpackdata.h"

/* Test POINTER for alignment with BYTE_COUNT sized quantities */
#define is_aligned(POINTER, BYTE_COUNT) \
  (((uintptr_t) (const void *)(POINTER)) % (BYTE_COUNT) == 0)

/***************************************************************************
 * msr3_unpack_mseed3:
 *
 * Unpack a miniSEED 3.x data record and populate a MS3Record struct.
 * All approriate fields are byteswapped, if needed, and pointers to
 * structured data are set.
 *
 * If 'dataflag' is true the data samples are unpacked/decompressed
 * and the MS3Record->datasamples pointer is set appropriately.  The
 * data samples will be either 32-bit integers, 32-bit floats or
 * 64-bit floats (doubles) with the same byte order as the host
 * machine.  The MS3Record->numsamples will be set to the actual
 * number of samples unpacked/decompressed and MS3Record->sampletype
 * will indicated the sample type.
 *
 * All appropriate values will be byte-swapped to the host order,
 * including the data samples.
 *
 * All MS3Record struct values, including data samples and data
 * samples will be overwritten by subsequent calls to this function.
 *
 * If the 'msr' struct is NULL it will be allocated.
 *
 * Returns MS_NOERROR and populates the MS3Record struct at *ppmsr on
 * success, otherwise returns a libmseed error code (listed in
 * libmseed.h).
 ***************************************************************************/
int
msr3_unpack_mseed3 (char *record, int reclen, MS3Record **ppmsr,
                    int8_t dataflag, int8_t verbose)
{
  int8_t swapflag = 0;
  uint8_t tsidlength = 0;
  int retval;

  MS3Record *msr = NULL;

  if (!record)
  {
    ms_log (2, "msr3_unpack_mseed3(): record argument must be specified\n");
    return MS_GENERROR;
  }

  if (!ppmsr)
  {
    ms_log (2, "msr3_unpack_mseed3(): ppmsr argument cannot be NULL\n");
    return MS_GENERROR;
  }

  /* Verify that passed record length is within supported range */
  if (reclen < MINRECLEN || reclen > MAXRECLEN)
  {
    ms_log (2, "msr3_unpack_mseed2(): Record length is out of allowed range: %d\n", reclen);
    return MS_OUTOFRANGE;
  }

  /* Verify that record includes a valid header */
  if (!MS3_ISVALIDHEADER (record))
  {
    ms_log (2, "msr3_unpack_mseed3() Record header unrecognized, not a valid miniSEED record\n");
    return MS_NOTSEED;
  }

  /* Initialize the MS3Record */
  if (!(*ppmsr = msr3_init (*ppmsr)))
    return MS_GENERROR;

  /* Shortcut pointer, historical and helps readability */
  msr = *ppmsr;

  /* Set raw record pointer and record length */
  msr->record = record;
  msr->reclen = reclen;

  /* miniSEED 3 is little endian */
  swapflag = (ms_bigendianhost()) ? 1 : 0;

  /* Report byte swapping status */
  if (verbose > 2)
  {
    if (swapflag)
      ms_log (1, "%s: Byte swapping needed for unpacking of header\n", msr->tsid);
    else
      ms_log (1, "%s: Byte swapping NOT needed for unpacking of header\n", msr->tsid);
  }

  /* Populate the header fields */
  msr->formatversion = *pMS3FSDH_FORMATVERSION(record);
  msr->flags = *pMS3FSDH_FLAGS(record);
  msr->starttime = ms_time2nstime (HO2u(*pMS3FSDH_YEAR(record), swapflag),
                                   HO2u(*pMS3FSDH_DAY(record), swapflag),
                                   *pMS3FSDH_HOUR(record),
                                   *pMS3FSDH_MIN(record),
                                   *pMS3FSDH_SEC(record),
                                   HO4u(*pMS3FSDH_NSEC(record), swapflag));
  msr->encoding = *pMS3FSDH_ENCODING(record);
  msr->samprate = HO8f(*pMS3FSDH_SAMPLERATE(record), swapflag);
  msr->samplecnt = HO4u(*pMS3FSDH_NUMSAMPLES(record), swapflag);
  msr->crc = HO4u(*pMS3FSDH_CRC(record), swapflag);
  msr->pubversion = *pMS3FSDH_PUBVERSION(record);

  tsidlength = *pMS3FSDH_TSIDLENGTH(record);

  if (tsidlength > sizeof(msr->tsid))
  {
    ms_log (2, "msr3_unpack_mseed2(%.*s): Time series identifier is longer (%d) than supported (%d)\n",
            tsidlength, pMS3FSDH_TSID(record), tsidlength, (int)sizeof(msr->tsid));
    return MS_GENERROR;
  }

  strncpy (msr->tsid, pMS3FSDH_TSID(record), tsidlength);

  msr->extralength = HO2u(*pMS3FSDH_EXTRALENGTH(record), swapflag);
  if (msr->extralength)
  {
    if ((msr->extra = malloc (msr->extralength)) == NULL)
    {
      ms_log (2, "msr3_unpack_mseed2(%s): Cannot allocate memory for extra headers\n", msr->tsid);
      return MS_GENERROR;
    }

    memcpy (msr->extra, record + MS3FSDH_LENGTH + tsidlength, msr->extralength);
  }

  msr->datalength = HO2u(*pMS3FSDH_DATALENGTH(record), swapflag);

  /* Unpack the data samples if requested */
  if (dataflag && msr->samplecnt > 0)
  {
    retval = msr3_unpack_data (msr, swapflag, verbose);

    if (retval < 0)
      return retval;
    else
      msr->numsamples = retval;
  }
  else
  {
    if (msr->datasamples)
      free (msr->datasamples);

    msr->datasamples = 0;
    msr->numsamples = 0;
  }

  return MS_NOERROR;
} /* End of msr3_unpack_mseed3() */

/***************************************************************************
 * msr3_unpack_mseed2:
 *
 * Unpack a miniSEED 2.x data record and populate a MS3Record struct.
 * All approriate fields are byteswapped, if needed, and pointers to
 * structured data are set.
 *
 * If 'dataflag' is true the data samples are unpacked/decompressed
 * and the MS3Record->datasamples pointer is set appropriately.  The
 * data samples will be either 32-bit integers, 32-bit floats or
 * 64-bit floats (doubles) with the same byte order as the host
 * machine.  The MS3Record->numsamples will be set to the actual
 * number of samples unpacked/decompressed and MS3Record->sampletype
 * will indicated the sample type.
 *
 * All appropriate values will be byte-swapped to the host order,
 * including the data samples.
 *
 * All MS3Record struct values, including data samples and data
 * samples will be overwritten by subsequent calls to this function.
 *
 * If the 'msr' struct is NULL it will be allocated.
 *
 * Returns MS_NOERROR and populates the MS3Record struct at *ppmsr on
 * success, otherwise returns a libmseed error code (listed in
 * libmseed.h).
 ***************************************************************************/
int
msr3_unpack_mseed2 (char *record, int reclen, MS3Record **ppmsr,
                    int8_t dataflag, int8_t verbose)
{
  int8_t swapflag = 0;
  int B1000offset = 0;
  int B1001offset = 0;
  int retval;

  MS3Record *msr = NULL;
  char errortsid[50];

  int ione = 1;
  int64_t ival;
  double dval;
  char sval[64];

  /* For blockette parsing */
  int blkt_offset;
  int blkt_count = 0;
  int blkt_length;
  int blkt_end = 0;
  uint16_t blkt_type;
  uint16_t next_blkt;

  MSEHEventDetection eventdetection;
  MSEHCalibration calibration;
  MSEHTimingException exception;

  if (!record)
  {
    ms_log (2, "msr3_unpack_mseed2(): record argument must be specified\n");
    return MS_GENERROR;
  }

  if (!ppmsr)
  {
    ms_log (2, "msr3_unpack_mseed2(): ppmsr argument must be specified\n");
    return MS_GENERROR;
  }

  /* Verify that passed record length is within supported range */
  if (reclen < 64 || reclen > MAXRECLEN)
  {
    ms2_recordtsid (record, errortsid);
    ms_log (2, "msr3_unpack_mseed2(%s): Record length is out of allowd range: %d\n",
            errortsid, reclen);

    return MS_OUTOFRANGE;
  }

  /* Verify that record includes a valid header */
  if (!MS2_ISVALIDHEADER (record))
  {
    ms2_recordtsid (record, errortsid);
    ms_log (2, "msr3_unpack_mseed2(%s) Record header unrecognized, not a valid miniSEED record\n",
            errortsid);

    return MS_NOTSEED;
  }

  /* Initialize the MS3Record */
  if (!(*ppmsr = msr3_init (*ppmsr)))
    return MS_GENERROR;

  /* Shortcut pointer, historical and helps readability */
  msr = *ppmsr;

  /* Set raw record pointer and record length */
  msr->record = record;
  msr->reclen = reclen;

  /* Check to see if byte swapping is needed by testing the year and day */
  if (!MS_ISVALIDYEARDAY (*pMS2FSDH_YEAR (record), *pMS2FSDH_DAY (record)))
    swapflag = 1;

  /* Report byte swapping status */
  if (verbose > 2)
  {
    if (swapflag)
      ms_log (1, "%s: Byte swapping needed for unpacking of header\n", msr->tsid);
    else
      ms_log (1, "%s: Byte swapping NOT needed for unpacking of header\n", msr->tsid);
  }

  /* Populate some of the common header fields */
  ms2_recordtsid (record, msr->tsid);
  msr->formatversion = 2;
  msr->samprate = ms_nomsamprate (HO2d(*pMS2FSDH_SAMPLERATEFACT (record), swapflag),
                                  HO2d(*pMS2FSDH_SAMPLERATEMULT (record), swapflag));
  msr->samplecnt = HO2u(*pMS2FSDH_NUMSAMPLES (record), swapflag);

  /* Map data quality indicator to publication version */
  if (*pMS2FSDH_DATAQUALITY (record) == 'M')
    msr->pubversion = 4;
  else if (*pMS2FSDH_DATAQUALITY (record) == 'Q')
    msr->pubversion = 3;
  else if (*pMS2FSDH_DATAQUALITY (record) == 'D')
    msr->pubversion = 2;
  else if (*pMS2FSDH_DATAQUALITY (record) == 'R')
    msr->pubversion = 1;
  else
    msr->pubversion = 0;

  /* Map activity bits */
  if (*pMS2FSDH_ACTFLAGS(record) & 0x01) /* Bit 0 */
    msr->flags |= 0x01;
  if (*pMS2FSDH_ACTFLAGS(record) & 0x04) /* Bit 2 */
    mseh_set_boolean (msr, &ione, "FDSN", "Event", "Begin");
  if (*pMS2FSDH_ACTFLAGS(record) & 0x08) /* Bit 3 */
    mseh_set_boolean (msr, &ione, "FDSN", "Event", "End");
  if (*pMS2FSDH_ACTFLAGS(record) & 0x10) /* Bit 4 */
  {
    ival = 1;
    mseh_set_int64_t (msr, &ival, "FDSN", "Time", "LeapSecond");
  }
  if (*pMS2FSDH_ACTFLAGS(record) & 0x20) /* Bit 5 */
  {
    ival = -1;
    mseh_set_int64_t (msr, &ival, "FDSN", "Time", "LeapSecond");
  }
  if (*pMS2FSDH_ACTFLAGS(record) & 0x40) /* Bit 6 */
    mseh_set_boolean (msr, &ione, "FDSN", "Event", "InProgress");

  /* Map I/O and clock flags */
  if (*pMS2FSDH_IOFLAGS(record) & 0x01) /* Bit 0 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "StationVolumeParityError");
  if (*pMS2FSDH_IOFLAGS(record) & 0x02) /* Bit 1 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "LongRecordRead");
  if (*pMS2FSDH_IOFLAGS(record) & 0x04) /* Bit 2 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "ShortRecordRead");
  if (*pMS2FSDH_IOFLAGS(record) & 0x08) /* Bit 3 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "StartOfTimeSeries");
  if (*pMS2FSDH_IOFLAGS(record) & 0x10) /* Bit 4 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "EndOfTimeSeries");
  if (*pMS2FSDH_IOFLAGS(record) & 0x20) /* Bit 5 */
    msr->flags |= 0x04;

  /* Map data quality flags */
  if (*pMS2FSDH_DQFLAGS(record) & 0x01) /* Bit 0 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "AmplifierSaturation");
  if (*pMS2FSDH_DQFLAGS(record) & 0x02) /* Bit 1 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "DigitizerClipping");
  if (*pMS2FSDH_DQFLAGS(record) & 0x04) /* Bit 2 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "Spikes");
  if (*pMS2FSDH_DQFLAGS(record) & 0x08) /* Bit 3 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "Glitches");
  if (*pMS2FSDH_DQFLAGS(record) & 0x10) /* Bit 4 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "MissingData");
  if (*pMS2FSDH_DQFLAGS(record) & 0x20) /* Bit 5 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "TelemetrySyncError");
  if (*pMS2FSDH_DQFLAGS(record) & 0x40) /* Bit 6 */
    mseh_set_boolean (msr, &ione, "FDSN", "Flags", "FilterCharging");
  if (*pMS2FSDH_DQFLAGS(record) & 0x80) /* Bit 7 */
    msr->flags |= 0x02;

  ival = HO4u(*pMS2FSDH_TIMECORRECT(record), swapflag);
  if (ival != 0)
  {
    dval = ival / 10000.0;
    mseh_set_double (msr, &dval, "FDSN", "Time", "Correction");
  }

  /* Traverse the blockettes */
  blkt_offset = HO2u(*pMS2FSDH_BLOCKETTEOFFSET (record), swapflag);

  while ((blkt_offset != 0) &&
         (blkt_offset < reclen) &&
         (blkt_offset < MAXRECLEN))
  {
    /* Every blockette has a similar 4 byte header: type and next */
    memcpy (&blkt_type, record + blkt_offset, 2);
    memcpy (&next_blkt, record + blkt_offset + 2, 2);

    if (swapflag)
    {
      ms_gswap2 (&blkt_type);
      ms_gswap2 (&next_blkt);
    }

    /* Get blockette length */
    blkt_length = ms2_blktlen (blkt_type, record + blkt_offset, swapflag);

    if (blkt_length == 0)
    {
      ms_log (2, "msr3_unpack_mseed2(%s): Unknown blockette length for type %d\n",
              msr->tsid, blkt_type);
      break;
    }

    /* Make sure blockette is contained within the msrecord buffer */
    if ((blkt_offset + blkt_length) > reclen)
    {
      ms_log (2, "msr3_unpack_mseed2(%s): Blockette %d extends beyond record size, truncated?\n",
              msr->tsid, blkt_type);
      break;
    }

    blkt_end = blkt_offset + blkt_length;

    if (blkt_type == 100)
    {
      msr->samprate = HO4f(*pMS2B100_SAMPRATE(record + blkt_offset), swapflag);
    }

    /* Blockette 200, generic event detection */
    else if (blkt_type == 200)
    {
      strncpy (eventdetection.type, "GENERIC", sizeof (eventdetection.type));
      ms_strncpcleantail (eventdetection.detector, pMS2B200_DETECTOR (record + blkt_offset), 24);
      eventdetection.signalamplitude = HO4f (*pMS2B200_AMPLITUDE (record + blkt_offset), swapflag);
      eventdetection.signalperiod = HO4f (*pMS2B200_PERIOD (record + blkt_offset), swapflag);
      eventdetection.backgroundestimate = HO4f (*pMS2B200_BACKGROUNDEST (record + blkt_offset), swapflag);

      /* If bit 2 is set, set compression wave according to bit 0 */
      if (*pMS2B200_FLAGS (record + blkt_offset) & 0x04)
      {
        if (*pMS2B200_FLAGS (record + blkt_offset) & 0x01)
          strncpy (eventdetection.detectionwave, "DILATATION", sizeof (eventdetection.detectionwave));
        else
          strncpy (eventdetection.detectionwave, "COMPRESSION", sizeof (eventdetection.detectionwave));
      }
      else
        eventdetection.detectionwave[0] = '\0';

      if (*pMS2B200_FLAGS (record + blkt_offset) & 0x02)
        strncpy (eventdetection.units, "DECONVOLVED", sizeof (eventdetection.units));
      else
        strncpy (eventdetection.units, "COUNTS", sizeof (eventdetection.units));

      eventdetection.onsettime = ms_time2nstime (HO2u (*pMS2B200_YEAR (record + blkt_offset), swapflag),
                                                 HO2u (*pMS2B200_DAY (record + blkt_offset), swapflag),
                                                 *pMS2B200_HOUR (record + blkt_offset),
                                                 *pMS2B200_MIN (record + blkt_offset),
                                                 *pMS2B200_SEC (record + blkt_offset),
                                                 (uint32_t)HO2u (*pMS2B200_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (eventdetection.onsettime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B200_YEAR (record), swapflag),
                HO2u (*pMS2B200_DAY (record), swapflag),
                *pMS2B200_HOUR (record),
                *pMS2B200_MIN (record),
                *pMS2B200_SEC (record),
                HO2u (*pMS2B200_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      memset (eventdetection.snrvalues, 0, 6);
      eventdetection.medlookback = -1;
      eventdetection.medpickalgorithm = -1;
      eventdetection.next = NULL;

      if (mseh_add_event_detection (msr, &eventdetection, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 200 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 201, Murdock event detection */
    else if (blkt_type == 201)
    {
      strncpy (eventdetection.type, "MURDOCK", sizeof (eventdetection.type));
      ms_strncpcleantail (eventdetection.detector, pMS2B201_DETECTOR (record + blkt_offset), 24);
      eventdetection.signalamplitude = HO4f (*pMS2B201_AMPLITUDE (record + blkt_offset), swapflag);
      eventdetection.signalperiod = HO4f (*pMS2B201_PERIOD (record + blkt_offset), swapflag);
      eventdetection.backgroundestimate = HO4f (*pMS2B201_BACKGROUNDEST (record + blkt_offset), swapflag);

      /* If bit 0 is set, dilatation wave otherwise compression */
      if (*pMS2B201_FLAGS (record + blkt_offset) & 0x01)
        strncpy (eventdetection.detectionwave, "DILATATION", sizeof (eventdetection.detectionwave));
      else
        strncpy (eventdetection.detectionwave, "COMPRESSION", sizeof (eventdetection.detectionwave));

      eventdetection.onsettime = ms_time2nstime (HO2u (*pMS2B201_YEAR (record + blkt_offset), swapflag),
                                                 HO2u (*pMS2B201_DAY (record + blkt_offset), swapflag),
                                                 *pMS2B201_HOUR (record + blkt_offset),
                                                 *pMS2B201_MIN (record + blkt_offset),
                                                 *pMS2B201_SEC (record + blkt_offset),
                                                 (uint32_t)HO2u (*pMS2B201_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (eventdetection.onsettime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B201_YEAR (record), swapflag),
                HO2u (*pMS2B201_DAY (record), swapflag),
                *pMS2B201_HOUR (record),
                *pMS2B201_MIN (record),
                *pMS2B201_SEC (record),
                HO2u (*pMS2B201_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      memcpy (eventdetection.snrvalues, pMS2B201_SNRVALUES (record + blkt_offset), 6);
      eventdetection.medlookback = *pMS2B201_LOOPBACK (record + blkt_offset);
      eventdetection.medpickalgorithm = *pMS2B201_PICKALGORITHM (record + blkt_offset);
      eventdetection.next = NULL;

      if (mseh_add_event_detection (msr, &eventdetection, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 201 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 300, step calibration */
    else if (blkt_type == 300)
    {
      strncpy (calibration.type, "STEP", sizeof (calibration.type));

      calibration.begintime = ms_time2nstime (HO2u (*pMS2B300_YEAR (record + blkt_offset), swapflag),
                                              HO2u (*pMS2B300_DAY (record + blkt_offset), swapflag),
                                              *pMS2B300_HOUR (record + blkt_offset),
                                              *pMS2B300_MIN (record + blkt_offset),
                                              *pMS2B300_SEC (record + blkt_offset),
                                              (uint32_t)HO2u (*pMS2B300_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (calibration.begintime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B300_YEAR (record), swapflag),
                HO2u (*pMS2B300_DAY (record), swapflag),
                *pMS2B300_HOUR (record),
                *pMS2B300_MIN (record),
                *pMS2B300_SEC (record),
                HO2u (*pMS2B300_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      calibration.endtime = NSTERROR;
      calibration.steps = *pMS2B300_NUMCALIBRATIONS(record + blkt_offset);

      /* If bit 0 is set, first puluse is positive */
      calibration.firstpulsepositive = -1;
      if (*pMS2B300_FLAGS(record + blkt_offset) & 0x01)
        calibration.firstpulsepositive = 1;

      /* If bit 1 is set, calibration's alternate sign */
      calibration.alternatesign = -1;
      if (*pMS2B300_FLAGS(record + blkt_offset) & 0x02)
        calibration.alternatesign = 1;

      /* If bit 2 is set, calibration is automatic, otherwise manual */
      if (*pMS2B300_FLAGS(record + blkt_offset) & 0x04)
        strncpy (calibration.trigger, "AUTOMATIC", sizeof (calibration.trigger));
      else
        strncpy (calibration.trigger, "MANUAL", sizeof (calibration.trigger));

      /* If bit 3 is set, continued from previous record */
      calibration.continued = -1;
      if (*pMS2B300_FLAGS(record + blkt_offset) & 0x08)
        calibration.continued = 1;

      calibration.duration = (double)(HO4u (*pMS2B300_STEPDURATION (record + blkt_offset), swapflag) / 10000.0);
      calibration.stepbetween = (double)(HO4u (*pMS2B300_INTERVALDURATION (record + blkt_offset), swapflag) / 10000.0);
      calibration.amplitude = HO4f (*pMS2B300_AMPLITUDE (record + blkt_offset), swapflag);
      ms_strncpcleantail (calibration.inputchannel, pMS2B300_INPUTCHANNEL (record + blkt_offset), 3);
      calibration.inputunits[0] = '\0';
      calibration.amplituderange[0] = '\0';
      calibration.sineperiod = 0.0;
      calibration.refamplitude = (double) (HO4u (*pMS2B300_REFERENCEAMPLITUDE (record + blkt_offset), swapflag));
      ms_strncpcleantail (calibration.coupling, pMS2B300_COUPLING (record + blkt_offset), 12);
      ms_strncpcleantail (calibration.rolloff, pMS2B300_ROLLOFF (record + blkt_offset), 12);
      calibration.noise[0] = '\0';
      calibration.next = NULL;

      if (mseh_add_calibration (msr, &calibration, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 300 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 310, sine calibration */
    else if (blkt_type == 310)
    {
      strncpy (calibration.type, "SINE", sizeof (calibration.type));

      calibration.begintime = ms_time2nstime (HO2u (*pMS2B310_YEAR (record + blkt_offset), swapflag),
                                              HO2u (*pMS2B310_DAY (record + blkt_offset), swapflag),
                                              *pMS2B310_HOUR (record + blkt_offset),
                                              *pMS2B310_MIN (record + blkt_offset),
                                              *pMS2B310_SEC (record + blkt_offset),
                                              (uint32_t)HO2u (*pMS2B310_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (calibration.begintime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B310_YEAR (record), swapflag),
                HO2u (*pMS2B310_DAY (record), swapflag),
                *pMS2B310_HOUR (record),
                *pMS2B310_MIN (record),
                *pMS2B310_SEC (record),
                HO2u (*pMS2B310_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      calibration.endtime = NSTERROR;
      calibration.steps = -1;
      calibration.firstpulsepositive = -1;
      calibration.alternatesign = -1;

      /* If bit 2 is set, calibration is automatic, otherwise manual */
      if (*pMS2B310_FLAGS(record + blkt_offset) & 0x04)
        strncpy (calibration.trigger, "AUTOMATIC", sizeof (calibration.trigger));
      else
        strncpy (calibration.trigger, "MANUAL", sizeof (calibration.trigger));

      /* If bit 3 is set, continued from previous record */
      calibration.continued = -1;
      if (*pMS2B310_FLAGS(record + blkt_offset) & 0x08)
        calibration.continued = 1;

      calibration.amplituderange[0] = '\0';
      /* If bit 4 is set, peak to peak amplitude */
      if (*pMS2B310_FLAGS(record + blkt_offset) & 0x10)
        strncpy (calibration.amplituderange, "PEAKTOPEAK", sizeof (calibration.amplituderange));
      /* Otherwise, if bit 5 is set, zero to peak amplitude */
      else if (*pMS2B310_FLAGS(record + blkt_offset) & 0x20)
        strncpy (calibration.amplituderange, "ZEROTOPEAK", sizeof (calibration.amplituderange));
      /* Otherwise, if bit 6 is set, RMS amplitude */
      else if (*pMS2B310_FLAGS(record + blkt_offset) & 0x40)
        strncpy (calibration.amplituderange, "RMS", sizeof (calibration.amplituderange));

      calibration.duration = (double)(HO4u (*pMS2B310_DURATION (record + blkt_offset), swapflag) / 10000.0);
      calibration.sineperiod = HO4f(*pMS2B310_PERIOD(record + blkt_offset), swapflag);
      calibration.amplitude = HO4f (*pMS2B310_AMPLITUDE (record + blkt_offset), swapflag);
      ms_strncpcleantail (calibration.inputchannel, pMS2B310_INPUTCHANNEL (record + blkt_offset), 3);
      calibration.refamplitude = (double) (HO4u (*pMS2B310_REFERENCEAMPLITUDE (record + blkt_offset), swapflag));
      calibration.stepbetween = 0.0;
      calibration.inputunits[0] = '\0';
      ms_strncpcleantail (calibration.coupling, pMS2B310_COUPLING (record + blkt_offset), 12);
      ms_strncpcleantail (calibration.rolloff, pMS2B310_ROLLOFF (record + blkt_offset), 12);
      calibration.noise[0] = '\0';
      calibration.next = NULL;

      if (mseh_add_calibration (msr, &calibration, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 310 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 320, pseudo-random calibration */
    else if (blkt_type == 320)
    {
      strncpy (calibration.type, "PSEUDORANDOM", sizeof (calibration.type));

      calibration.begintime = ms_time2nstime (HO2u (*pMS2B320_YEAR (record + blkt_offset), swapflag),
                                              HO2u (*pMS2B320_DAY (record + blkt_offset), swapflag),
                                              *pMS2B320_HOUR (record + blkt_offset),
                                              *pMS2B320_MIN (record + blkt_offset),
                                              *pMS2B320_SEC (record + blkt_offset),
                                              (uint32_t)HO2u (*pMS2B320_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (calibration.begintime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B320_YEAR (record), swapflag),
                HO2u (*pMS2B320_DAY (record), swapflag),
                *pMS2B320_HOUR (record),
                *pMS2B320_MIN (record),
                *pMS2B320_SEC (record),
                HO2u (*pMS2B320_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      calibration.endtime = NSTERROR;
      calibration.steps = -1;
      calibration.firstpulsepositive = -1;
      calibration.alternatesign = -1;

      /* If bit 2 is set, calibration is automatic, otherwise manual */
      if (*pMS2B320_FLAGS(record + blkt_offset) & 0x04)
        strncpy (calibration.trigger, "AUTOMATIC", sizeof (calibration.trigger));
      else
        strncpy (calibration.trigger, "MANUAL", sizeof (calibration.trigger));

      /* If bit 3 is set, continued from previous record */
      calibration.continued = -1;
      if (*pMS2B320_FLAGS(record + blkt_offset) & 0x08)
        calibration.continued = 1;

      calibration.amplituderange[0] = '\0';
      /* If bit 4 is set, peak to peak amplitude */
      if (*pMS2B320_FLAGS(record + blkt_offset) & 0x10)
        strncpy (calibration.amplituderange, "RANDOM", sizeof (calibration.amplituderange));

      calibration.duration = (double)(HO4u (*pMS2B320_DURATION (record + blkt_offset), swapflag) / 10000.0);
      calibration.amplitude = HO4f (*pMS2B320_PTPAMPLITUDE(record + blkt_offset), swapflag);
      ms_strncpcleantail (calibration.inputchannel, pMS2B320_INPUTCHANNEL (record + blkt_offset), 3);
      calibration.refamplitude = (double) (HO4u (*pMS2B320_REFERENCEAMPLITUDE (record + blkt_offset), swapflag));
      calibration.sineperiod = 0.0;
      calibration.stepbetween = 0.0;
      calibration.inputunits[0] = '\0';
      ms_strncpcleantail (calibration.coupling, pMS2B320_COUPLING (record + blkt_offset), 12);
      ms_strncpcleantail (calibration.rolloff, pMS2B320_ROLLOFF (record + blkt_offset), 12);
      ms_strncpcleantail (calibration.noise, pMS2B320_NOISETYPE (record + blkt_offset), 8);
      calibration.next = NULL;

      if (mseh_add_calibration (msr, &calibration, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 320 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 390, generic calibration */
    else if (blkt_type == 390)
    {
      strncpy (calibration.type, "GENERIC", sizeof (calibration.type));

      calibration.begintime = ms_time2nstime (HO2u (*pMS2B390_YEAR (record + blkt_offset), swapflag),
                                              HO2u (*pMS2B390_DAY (record + blkt_offset), swapflag),
                                              *pMS2B390_HOUR (record + blkt_offset),
                                              *pMS2B390_MIN (record + blkt_offset),
                                              *pMS2B390_SEC (record + blkt_offset),
                                              (uint32_t)HO2u (*pMS2B390_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (calibration.begintime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B390_YEAR (record), swapflag),
                HO2u (*pMS2B390_DAY (record), swapflag),
                *pMS2B390_HOUR (record),
                *pMS2B390_MIN (record),
                *pMS2B390_SEC (record),
                HO2u (*pMS2B390_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      calibration.endtime = NSTERROR;
      calibration.steps = -1;
      calibration.firstpulsepositive = -1;
      calibration.alternatesign = -1;

      /* If bit 2 is set, calibration is automatic, otherwise manual */
      if (*pMS2B390_FLAGS(record + blkt_offset) & 0x04)
        strncpy (calibration.trigger, "AUTOMATIC", sizeof (calibration.trigger));
      else
        strncpy (calibration.trigger, "MANUAL", sizeof (calibration.trigger));

      /* If bit 3 is set, continued from previous record */
      calibration.continued = -1;
      if (*pMS2B390_FLAGS(record + blkt_offset) & 0x08)
        calibration.continued = 1;

      calibration.amplituderange[0] = '\0';
      calibration.duration = (double)(HO4u (*pMS2B390_DURATION (record + blkt_offset), swapflag) / 10000.0);
      calibration.amplitude = HO4f (*pMS2B390_AMPLITUDE(record + blkt_offset), swapflag);
      ms_strncpcleantail (calibration.inputchannel, pMS2B390_INPUTCHANNEL (record + blkt_offset), 3);
      calibration.refamplitude = 0.0;
      calibration.sineperiod = 0.0;
      calibration.stepbetween = 0.0;
      calibration.inputunits[0] = '\0';
      calibration.coupling[0] = '\0';
      calibration.rolloff[0] = '\0';
      calibration.noise[0] = '\0';
      calibration.next = NULL;

      if (mseh_add_calibration (msr, &calibration, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 390 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 395, calibration abort */
    else if (blkt_type == 395)
    {
      strncpy (calibration.type, "ABORT", sizeof (calibration.type));

      calibration.begintime = NSTERROR;
      calibration.endtime = ms_time2nstime (HO2u (*pMS2B395_YEAR (record + blkt_offset), swapflag),
                                              HO2u (*pMS2B395_DAY (record + blkt_offset), swapflag),
                                              *pMS2B395_HOUR (record + blkt_offset),
                                              *pMS2B395_MIN (record + blkt_offset),
                                              *pMS2B395_SEC (record + blkt_offset),
                                              (uint32_t)HO2u (*pMS2B395_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (calibration.endtime == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B395_YEAR (record), swapflag),
                HO2u (*pMS2B395_DAY (record), swapflag),
                *pMS2B395_HOUR (record),
                *pMS2B395_MIN (record),
                *pMS2B395_SEC (record),
                HO2u (*pMS2B395_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      calibration.steps = -1;
      calibration.firstpulsepositive = -1;
      calibration.alternatesign = -1;
      calibration.trigger[0] = '\0';
      calibration.continued = -1;
      calibration.amplituderange[0] = '\0';
      calibration.duration = 0.0;
      calibration.amplitude = 0.0;
      calibration.inputchannel[0] = '\0';
      calibration.refamplitude = 0.0;
      calibration.sineperiod = 0.0;
      calibration.stepbetween = 0.0;
      calibration.inputunits[0] = '\0';
      calibration.coupling[0] = '\0';
      calibration.rolloff[0] = '\0';
      calibration.noise[0] = '\0';
      calibration.next = NULL;

      if (mseh_add_calibration (msr, &calibration, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 395 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }
    }

    /* Blockette 400, beam blockette */
    else if (blkt_type == 400)
    {
      ms_log (1, "msr3_unpack_mseed2(%s): WARNING Blockette 400 is present but discarded\n", msr->tsid);
    }

    /* Blockette 400, beam delay blockette */
    else if (blkt_type == 405)
    {
      ms_log (1, "msr3_unpack_mseed2(%s): WARNING Blockette 405 is present but discarded\n", msr->tsid);
    }

    /* Blockette 500, timing blockette */
    else if (blkt_type == 500)
    {
      exception.vcocorrection = HO4f(*pMS2B500_VCOCORRECTION(record + blkt_offset), swapflag);

      exception.time = ms_time2nstime (HO2u (*pMS2B500_YEAR (record + blkt_offset), swapflag),
                                       HO2u (*pMS2B500_DAY (record + blkt_offset), swapflag),
                                       *pMS2B500_HOUR (record + blkt_offset),
                                       *pMS2B500_MIN (record + blkt_offset),
                                       *pMS2B500_SEC (record + blkt_offset),
                                       (uint32_t)HO2u (*pMS2B500_FSEC (record + blkt_offset), swapflag) * (NSTMODULUS / 10000));
      if (exception.time == NSTERROR)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
                msr->tsid,
                HO2u (*pMS2B500_YEAR (record), swapflag),
                HO2u (*pMS2B500_DAY (record), swapflag),
                *pMS2B500_HOUR (record),
                *pMS2B500_MIN (record),
                *pMS2B500_SEC (record),
                HO2u (*pMS2B500_FSEC (record), swapflag));
        return MS_GENERROR;
      }

      exception.usec = *pMS2B500_MICROSECOND(record + blkt_offset);
      exception.receptionquality = *pMS2B500_RECEPTIONQUALITY(record + blkt_offset);
      exception.count = HO4u(*pMS2B500_EXCEPTIONCOUNT(record + blkt_offset), swapflag);
      ms_strncpcleantail (exception.type, pMS2B500_EXCEPTIONTYPE (record + blkt_offset), 16);
      ms_strncpcleantail (exception.clockstatus, pMS2B500_CLOCKSTATUS (record + blkt_offset), 128);

      if (mseh_add_timing_exception (msr, &exception, NULL))
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Problem mapping Blockette 500 to extra headers\n", msr->tsid);
        return MS_GENERROR;
      }

      ms_strncpcleantail (sval, pMS2B500_CLOCKMODEL (record + blkt_offset), 32);
      mseh_set_bytes (msr, sval, strlen(sval), "FDSN", "Clock", "Model");
    }

    else if (blkt_type == 1000)
    {
      B1000offset = blkt_offset;

      /* Calculate record length in bytes as 2^(B1000->reclen) */
      msr->reclen = (uint32_t)1 << *pMS2B1000_RECLEN (record + blkt_offset);

      /* Compare against the specified length */
      if (msr->reclen != reclen && verbose)
      {
        ms_log (2, "msr3_unpack_mseed2(%s): Record length in Blockette 1000 (%d) != specified length (%d)\n",
                msr->tsid, msr->reclen, reclen);
      }

      msr->encoding = *pMS2B1000_ENCODING (record + blkt_offset);
    }

    else if (blkt_type == 1001)
    {
      B1001offset = blkt_offset;

      ival = *pMS2B1001_TIMINGQUALITY(record + blkt_offset);
      mseh_set_int64_t (msr, &ival, "FDSN", "Time", "Quality");
    }

    else if (blkt_type == 2000)
    {
      ms_log (1, "msr3_unpack_mseed2(%s): WARNING Blockette 2000 is present but discarded\n", msr->tsid);
    }

    else
    { /* Unknown blockette type */
      ms_log (1, "msr3_unpack_mseed2(%s): WARNING, unsupported blockette type %d, skipping\n", msr->tsid);
    }

    /* Check that the next blockette offset is beyond the current blockette */
    if (next_blkt && next_blkt < (blkt_offset + blkt_length))
    {
      ms_log (2, "msr2_unpack_mseed2(%s): Offset to next blockette (%d) is within current blockette ending at byte %d\n",
              msr->tsid, next_blkt, (blkt_offset + blkt_length));

      blkt_offset = 0;
    }
    /* Check that the offset is within record length */
    else if (next_blkt && next_blkt > reclen)
    {
      ms_log (2, "msr3_unpack_mseed2(%s): Offset to next blockette (%d) from type %d is beyond record length\n",
              msr->tsid, next_blkt, blkt_type);

      blkt_offset = 0;
    }
    else
    {
      blkt_offset = next_blkt;
    }

    blkt_count++;
  } /* End of while looping through blockettes */

  /* Check for a Blockette 1000 */
  if (B1000offset == 0)
  {
    if (verbose > 1)
    {
      ms_log (1, "%s: Warning: No Blockette 1000 found\n", msr->tsid);
    }
  }

  /* Check that the data offset is after the blockette chain */
  if (blkt_end &&
      HO2u(*pMS2FSDH_NUMSAMPLES(record), swapflag) &&
      HO2u(*pMS2FSDH_DATAOFFSET(record), swapflag) < blkt_end)
  {
    ms_log (1, "%s: Warning: Data offset in fixed header (%d) is within the blockette chain ending at %d\n",
            msr->tsid, HO2u(*pMS2FSDH_DATAOFFSET(record), swapflag), blkt_end);
  }

  /* Check that the blockette count matches the number parsed */
  if (*pMS2FSDH_NUMBLOCKETTES(record) != blkt_count)
  {
    ms_log (1, "%s: Warning: Number of blockettes in fixed header (%d) does not match the number parsed (%d)\n",
            msr->tsid, *pMS2FSDH_NUMBLOCKETTES(record), blkt_count);
  }

  /* Calculate start time */
  msr->starttime = ms_time2nstime (HO2u(*pMS2FSDH_YEAR (record), swapflag),
                                   HO2u(*pMS2FSDH_DAY (record), swapflag),
                                   *pMS2FSDH_HOUR (record),
                                   *pMS2FSDH_MIN (record),
                                   *pMS2FSDH_SEC (record),
                                   (uint32_t)HO2u(*pMS2FSDH_FSEC (record), swapflag) * (NSTMODULUS / 10000));
  if (msr->starttime == NSTERROR)
  {
    ms_log (2, "%s: Cannot time values to internal time: %d,%d,%d,%d,%d,%d\n",
            HO2u(*pMS2FSDH_YEAR (record), swapflag),
            HO2u(*pMS2FSDH_DAY (record), swapflag),
            *pMS2FSDH_HOUR (record),
            *pMS2FSDH_MIN (record),
            *pMS2FSDH_SEC (record),
            HO2u(*pMS2FSDH_FSEC (record), swapflag));
    return MS_GENERROR;
  }

  /* Check if a time correction is included and if it has been applied,
   * bit 1 of activity flags indicates if it has been appiled */
  if (HO4d (*pMS2FSDH_TIMECORRECT (record), swapflag) != 0 &&
      !(*pMS2FSDH_ACTFLAGS (record) & 0x02))
  {
    msr->starttime += (nstime_t)HO4d (*pMS2FSDH_TIMECORRECT (record), swapflag) * (NSTMODULUS / 10000);
  }

  /* Apply microsecond precision if Blockette 1001 is present */
  if (B1001offset)
  {
    msr->starttime += (nstime_t)*pMS2B1001_MICROSECOND (record + B1001offset) * (NSTMODULUS / 1000000);
  }

  /* Unpack the data samples if requested */
  if (dataflag && msr->samplecnt > 0)
  {
    int8_t dswapflag = swapflag;
    int8_t bigendianhost = ms_bigendianhost ();

    /* Determine byte order of the data and set the dswapflag as
       needed; if no Blkt1000 or UNPACK_DATA_BYTEORDER environment
       variable setting assume the order is the same as the header */
    if (B1000offset)
    {
      dswapflag = 0;

      /* If BE host and LE data need swapping */
      if (bigendianhost && *pMS2B1000_BYTEORDER (record + B1000offset) == 0)
        dswapflag = 1;
      /* If LE host and BE data (or bad byte order value) need swapping */
      else if (!bigendianhost && *pMS2B1000_BYTEORDER (record + B1000offset) > 0)
        dswapflag = 1;
    }

    if (verbose > 2 && dswapflag)
      ms_log (1, "%s: Byte swapping needed for unpacking of data samples\n", msr->tsid);
    else if (verbose > 2)
      ms_log (1, "%s: Byte swapping NOT needed for unpacking of data samples\n", msr->tsid);

    retval = msr3_unpack_data (msr, dswapflag, verbose);

    if (retval < 0)
      return retval;
    else
      msr->numsamples = retval;
  }
  else
  {
    if (msr->datasamples)
      free (msr->datasamples);

    msr->datasamples = 0;
    msr->numsamples = 0;
  }

  return MS_NOERROR;
} /* End of msr3_unpack_mseed2() */

/************************************************************************
 *  msr3_unpack_data:
 *
 *  Unpack miniSEED data samples for a given MS3Record.  The
 *  packed/encoded data is accessed in the record indicated by
 *  MS3Record->record and the unpacked samples are placed in
 *  MS3Record->datasamples.  The resulting data samples are either
 *  text characters, 32-bit integers, 32-bit floats or 64-bit floats
 *  in host byte order.
 *
 *  An internal buffer is allocated if the encoded data is not aligned
 *  for the sample size, which is a decent indicator of the alignment
 *  needed for decoding efficiently.
 *
 *  Return number of samples unpacked or negative libmseed error code.
 ************************************************************************/
int
msr3_unpack_data (MS3Record *msr, int swapflag, int8_t verbose)
{
  size_t datasize; /* byte size of data samples in record */
  int nsamples; /* number of samples unpacked */
  size_t unpacksize; /* byte size of unpacked samples */
  int8_t samplesize = 0; /* size of the data samples in bytes */
  uint32_t dataoffset = 0;
  const char *encoded = NULL;
  char *encoded_allocated = NULL;

  if (!msr)
    return MS_GENERROR;

  if (!msr->record)
  {
    ms_log (2, "msr3_unpack_data(%s): Raw record pointer is unset\n", msr->tsid);
    return MS_GENERROR;
  }

  /* Check for decode debugging environment variable */
  if (getenv ("DECODE_DEBUG"))
    decodedebug = 1;

  /* Sanity check record length */
  if (msr->reclen == -1)
  {
    ms_log (2, "msr3_unpack_data(%s): Record size unknown\n", msr->tsid);
    return MS_NOTSEED;
  }
  else if (msr->reclen < MINRECLEN || msr->reclen > MAXRECLEN)
  {
    ms_log (2, "msr3_unpack_data(%s): Unsupported record length: %d\n",
            msr->tsid, msr->reclen);
    return MS_OUTOFRANGE;
  }

  /* Determine offset to data */
  if (msr->formatversion == 3)
  {
    dataoffset = MS3FSDH_LENGTH + strlen(msr->tsid) + msr->extralength;
    datasize = msr->datalength;
  }
  else if (msr->formatversion == 2)
  {
    dataoffset = HO2u(*pMS2FSDH_DATAOFFSET(msr->record), swapflag);
    datasize = msr->reclen - dataoffset;
  }
  else
  {
    ms_log (2, "msr3_unpack_data(%s): Unrecognized format version: %d\n",
            msr->tsid, msr->formatversion);
    return MS_GENERROR;
  }

  /* Sanity check data offset before creating a pointer based on the value */
  if (dataoffset < MINRECLEN || dataoffset >= msr->reclen)
  {
    ms_log (2, "msr3_unpack_data(%s): data offset value is not valid: %d\n",
            msr->tsid, dataoffset);
    return MS_GENERROR;
  }

  switch (msr->encoding)
  {
  case DE_ASCII:
    samplesize = 1;
    break;
  case DE_INT16:
  case DE_INT32:
  case DE_FLOAT32:
  case DE_STEIM1:
  case DE_STEIM2:
  case DE_GEOSCOPE24:
  case DE_GEOSCOPE163:
  case DE_GEOSCOPE164:
  case DE_CDSN:
  case DE_SRO:
  case DE_DWWSSN:
    samplesize = 4;
    break;
  case DE_FLOAT64:
    samplesize = 8;
    break;
  default:
    samplesize = 0;
    break;
  }

  encoded = msr->record + dataoffset;

  /* Copy encoded data to aligned/malloc'd buffer if not aligned for sample size */
  if (!is_aligned(encoded, samplesize))
  {
    if ((encoded_allocated = malloc (datasize)) == NULL)
    {
      ms_log (2, "msr3_unpack_data(): Cannot allocate memory for encoded data\n");
      return MS_GENERROR;
    }

    memcpy (encoded_allocated, encoded, datasize);
    encoded = encoded_allocated;
  }

  /* Calculate buffer size needed for unpacked samples */
  unpacksize = msr->samplecnt * samplesize;

  /* (Re)Allocate space for the unpacked data */
  if (unpacksize > 0)
  {
    msr->datasamples = realloc (msr->datasamples, unpacksize);

    if (msr->datasamples == NULL)
    {
      ms_log (2, "msr3_unpack_data(%s): Cannot (re)allocate memory\n", msr->tsid);
      if (encoded_allocated)
        free (encoded_allocated);
      return MS_GENERROR;
    }
  }
  else
  {
    if (msr->datasamples)
      free (msr->datasamples);
    msr->datasamples = 0;
    msr->numsamples = 0;
  }

  if (verbose > 2)
    ms_log (1, "%s: Unpacking %" PRId64 " samples\n", msr->tsid, msr->samplecnt);

  /* Decode data samples according to encoding */
  switch (msr->encoding)
  {
  case DE_ASCII:
    if (verbose > 1)
      ms_log (1, "%s: Found ASCII data\n", msr->tsid);

    nsamples = msr->samplecnt;
    memcpy (msr->datasamples, encoded, nsamples);
    msr->sampletype = 'a';
    break;

  case DE_INT16:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking INT16 data samples\n", msr->tsid);

    nsamples = msr_decode_int16 ((int16_t *)encoded, msr->samplecnt,
                                 msr->datasamples, unpacksize, swapflag);

    msr->sampletype = 'i';
    break;

  case DE_INT32:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking INT32 data samples\n", msr->tsid);

    nsamples = msr_decode_int32 ((int32_t *)encoded, msr->samplecnt,
                                 msr->datasamples, unpacksize, swapflag);

    msr->sampletype = 'i';
    break;

  case DE_FLOAT32:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking FLOAT32 data samples\n", msr->tsid);

    nsamples = msr_decode_float32 ((float *)encoded, msr->samplecnt,
                                   msr->datasamples, unpacksize, swapflag);

    msr->sampletype = 'f';
    break;

  case DE_FLOAT64:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking FLOAT64 data samples\n", msr->tsid);

    nsamples = msr_decode_float64 ((double *)encoded, msr->samplecnt,
                                   msr->datasamples, unpacksize, swapflag);

    msr->sampletype = 'd';
    break;

  case DE_STEIM1:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking Steim1 data frames\n", msr->tsid);

    /* Always big endian Steim1 */
    swapflag = (ms_bigendianhost()) ? 0 : 1;

    nsamples = msr_decode_steim1 ((int32_t *)encoded, datasize, msr->samplecnt,
                                  msr->datasamples, unpacksize, msr->tsid, swapflag);

    if (nsamples < 0)
    {
      nsamples = MS_GENERROR;
      break;
    }

    msr->sampletype = 'i';
    break;

  case DE_STEIM2:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking Steim2 data frames\n", msr->tsid);

    /* Always big endian Steim2 */
    swapflag = (ms_bigendianhost()) ? 0 : 1;

    nsamples = msr_decode_steim2 ((int32_t *)encoded, datasize, msr->samplecnt,
                                  msr->datasamples, unpacksize, msr->tsid, swapflag);

    if (nsamples < 0)
    {
      nsamples = MS_GENERROR;
      break;
    }

    msr->sampletype = 'i';
    break;

  case DE_GEOSCOPE24:
  case DE_GEOSCOPE163:
  case DE_GEOSCOPE164:
    if (verbose > 1)
    {
      if (msr->encoding == DE_GEOSCOPE24)
        ms_log (1, "%s: Unpacking GEOSCOPE 24bit integer data samples\n", msr->tsid);
      if (msr->encoding == DE_GEOSCOPE163)
        ms_log (1, "%s: Unpacking GEOSCOPE 16bit gain ranged/3bit exponent data samples\n", msr->tsid);
      if (msr->encoding == DE_GEOSCOPE164)
        ms_log (1, "%s: Unpacking GEOSCOPE 16bit gain ranged/4bit exponent data samples\n", msr->tsid);
    }

    nsamples = msr_decode_geoscope ((char *)encoded, msr->samplecnt, msr->datasamples,
                                    unpacksize, msr->encoding, msr->tsid, swapflag);

    msr->sampletype = 'f';
    break;

  case DE_CDSN:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking CDSN encoded data samples\n", msr->tsid);

    nsamples = msr_decode_cdsn ((int16_t *)encoded, msr->samplecnt, msr->datasamples,
                                unpacksize, swapflag);

    msr->sampletype = 'i';
    break;

  case DE_SRO:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking SRO encoded data samples\n", msr->tsid);

    nsamples = msr_decode_sro ((int16_t *)encoded, msr->samplecnt, msr->datasamples,
                               unpacksize, msr->tsid, swapflag);

    msr->sampletype = 'i';
    break;

  case DE_DWWSSN:
    if (verbose > 1)
      ms_log (1, "%s: Unpacking DWWSSN encoded data samples\n", msr->tsid);

    nsamples = msr_decode_dwwssn ((int16_t *)encoded, msr->samplecnt, msr->datasamples,
                                  unpacksize, swapflag);

    msr->sampletype = 'i';
    break;

  default:
    ms_log (2, "%s: Unsupported encoding format %d (%s)\n",
            msr->tsid, msr->encoding, (char *)ms_encodingstr (msr->encoding));

    nsamples = MS_UNKNOWNFORMAT;
    break;
  }

  if (encoded_allocated)
    free (encoded_allocated);

  if (nsamples >= 0 && nsamples != msr->samplecnt)
  {
    ms_log (2, "msr3_unpack_data(%s): only decoded %d samples of %d expected\n",
            msr->tsid, nsamples, msr->samplecnt);
    return MS_GENERROR;
  }

  return nsamples;
} /* End of msr3_unpack_data() */


/***************************************************************************
 * ms_nomsamprate:
 *
 * Calculate a sample rate from SEED sample rate factor and multiplier
 * as stored in the fixed section header of data records.
 *
 * Returns the positive sample rate.
 ***************************************************************************/
double
ms_nomsamprate (int factor, int multiplier)
{
  double samprate = 0.0;

  if (factor > 0)
    samprate = (double)factor;
  else if (factor < 0)
    samprate = -1.0 / (double)factor;
  if (multiplier > 0)
    samprate = samprate * (double)multiplier;
  else if (multiplier < 0)
    samprate = -1.0 * (samprate / (double)multiplier);

  return samprate;
} /* End of ms_nomsamprate() */

/***************************************************************************
 * ms2_recordtsid:
 *
 * Generate a time series identifier string for a specified raw
 * miniSEED 2.x data record in the format: 'FDSN:NET.STA.[LOC:]CHAN'.
 * The supplied tsid buffer must have enough room for the resulting
 * string.
 *
 * Returns a pointer to the resulting string or NULL on error.
 ***************************************************************************/
char *
ms2_recordtsid (char *record, char *tsid)
{
  int idx;

  if (!record || !tsid)
    return NULL;

  idx = 0;
  idx += ms_strncpclean (tsid+idx,  "FDSN:", 5);
  idx += ms_strncpclean (tsid+idx,  pMS2FSDH_NETWORK(record), 2);
  idx += ms_strncpclean (tsid+idx,  ".", 1);
  idx += ms_strncpclean (tsid+idx,  pMS2FSDH_STATION(record), 5);
  idx += ms_strncpclean (tsid+idx,  ".", 1);

  if ( pMS2FSDH_LOCATION(record)[0] != ' ' &&
       pMS2FSDH_LOCATION(record)[0] != '\0')
  {
    idx += ms_strncpclean (tsid+idx,  pMS2FSDH_LOCATION(record), 2);
    idx += ms_strncpclean (tsid+idx,  ":", 1);
  }
  idx += ms_strncpclean (tsid+idx,  pMS2FSDH_CHANNEL(record), 3);

  return tsid;
} /* End of ms2_recordtsid() */

/***************************************************************************
 * ms2_blktdesc():
 *
 * Return a string describing a given blockette type or NULL if the
 * type is unknown.
 ***************************************************************************/
char *
ms2_blktdesc (uint16_t blkttype)
{
  switch (blkttype)
  {
  case 100:
    return "Sample Rate";
  case 200:
    return "Generic Event Detection";
  case 201:
    return "Murdock Event Detection";
  case 300:
    return "Step Calibration";
  case 310:
    return "Sine Calibration";
  case 320:
    return "Pseudo-random Calibration";
  case 390:
    return "Generic Calibration";
  case 395:
    return "Calibration Abort";
  case 400:
    return "Beam";
  case 500:
    return "Timing";
  case 1000:
    return "Data Only SEED";
  case 1001:
    return "Data Extension";
  case 2000:
    return "Opaque Data";
  } /* end switch */

  return NULL;

} /* End of ms2_blktdesc() */

/***************************************************************************
 * ms2_blktlen():
 *
 * Returns the total length of a given blockette type in bytes or 0 if
 * type unknown.
 ***************************************************************************/
uint16_t
ms2_blktlen (uint16_t blkttype, const char *blkt, flag swapflag)
{
  uint16_t blktlen = 0;

  switch (blkttype)
  {
  case 100: /* Sample Rate */
    blktlen = 12;
    break;
  case 200: /* Generic Event Detection */
    blktlen = 28;
    break;
  case 201: /* Murdock Event Detection */
    blktlen = 36;
    break;
  case 300: /* Step Calibration */
    blktlen = 32;
    break;
  case 310: /* Sine Calibration */
    blktlen = 32;
    break;
  case 320: /* Pseudo-random Calibration */
    blktlen = 28;
    break;
  case 390: /* Generic Calibration */
    blktlen = 28;
    break;
  case 395: /* Calibration Abort */
    blktlen = 16;
    break;
  case 400: /* Beam */
    blktlen = 16;
    break;
  case 500: /* Timing */
    blktlen = 8;
    break;
  case 1000: /* Data Only SEED */
    blktlen = 8;
    break;
  case 1001: /* Data Extension */
    blktlen = 8;
    break;
  case 2000: /* Opaque Data */
    /* First 2-byte field after the blockette header is the length */
    if (blkt)
    {
      memcpy ((void *)&blktlen, blkt + 4, sizeof (int16_t));
      if (swapflag)
        ms_gswap2 (&blktlen);
    }
    break;
  } /* end switch */

  return blktlen;

} /* End of ms2_blktlen() */
