/* packet-trdp_spy.cpp
 * Routines for Train Real Time Data Protocol
 * Copyright 2012, Florian Weispfenning <florian.weispfenning@de.transport.bombardier.com>
 *
 * Extended to work with complex dataset and makeover for Wireshark 2.6 -- 3
 * (c) 2019, Thorsten Schulz <thorsten.schulz@uni-rostock.de>
 *
 * The new display-filter approach contains many aspects from the wimaxasncp dissector by Stephen Croll
 *
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 *
 * Copied from WHATEVER_FILE_YOU_USED (where "WHATEVER_FILE_YOU_USED"
 * is a dissector file; if you just copied this from README.developer,
 * don't bother with the "Copied from" - you don't even need to put
 * in a "Copied from" if you copied an existing dissector, especially
 * if the bulk of the code in the new dissector is your code)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


# include "config.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <epan/tvbuff.h>
#include <epan/packet_info.h>
#include <epan/column-utils.h>
#include <epan/packet.h>
#include <epan/prefs.h>
#include <epan/strutil.h>
#include <epan/expert.h>
#include <wsutil/report_message.h>
#include "trdp_env.h"
#include "trdpConfigHandler.h"

//To Debug
//#define PRINT_DEBUG2

#ifdef PRINT_DEBUG
#define PRNT(a) a
#else
#define PRNT(a)
#endif

#ifdef PRINT_DEBUG2
#define PRNT2(a) a
#else
#define PRNT2(a)
#endif

#define API_TRACE PRNT2(fprintf(stderr, "%s:%d : %s\n",__FILE__, __LINE__, __FUNCTION__))

#if (VERSION_MAJOR <= 3)
#define tvb_get_gint8(  tvb, offset) ((gint8 ) tvb_get_guint8(tvb, offset))
#define tvb_get_ntohis( tvb, offset) ((gint16) tvb_get_ntohs( tvb, offset))
#define tvb_get_ntohil( tvb, offset) ((gint32) tvb_get_ntohl( tvb, offset))
#define tvb_get_ntohi64(tvb, offset) ((gint64) tvb_get_ntoh64(tvb, offset))
#endif

/* Initialize the protocol and registered fields */
static int proto_trdp_spy = -1;
//static int proto_trdp_spy_TCP = -1;

/*For All*/
static int hf_trdp_spy_sequencecounter = -1;    /*uint32*/
static int hf_trdp_spy_protocolversion = -1;    /*uint16*/
static int hf_trdp_spy_type = -1;               /*uint16*/
static int hf_trdp_spy_etb_topocount = -1;      /*uint32*/
static int hf_trdp_spy_op_trn_topocount = -1;   /*uint32*/
static int hf_trdp_spy_comid = -1;              /*uint32*/
static int hf_trdp_spy_datasetlength = -1;      /*uint16*/
static int hf_trdp_spy_padding = -1;            /*bytes */

/*For All (user data)*/
static int hf_trdp_spy_fcs_head = -1;           /*uint32*/
static int hf_trdp_spy_fcs_head_calc = -1;      /*uint32*/
static int hf_trdp_spy_fcs_head_data = -1;      /*uint32*/
static int hf_trdp_spy_userdata = -1;           /* userdata */

/*needed only for PD messages*/
static int hf_trdp_spy_reserved    = -1;        /*uint32*/
static int hf_trdp_spy_reply_comid = -1;        /*uint32*/   /*for MD-family only*/
static int hf_trdp_spy_reply_ipaddress = -1;    /*uint32*/
static int hf_trdp_spy_isPD = -1;               /* flag */

/* needed only for MD messages*/
static int hf_trdp_spy_replystatus = -1;        /*uint32*/
static int hf_trdp_spy_sessionid0 = -1;         /*uint32*/
static int hf_trdp_spy_sessionid1 = -1;         /*uint32*/
static int hf_trdp_spy_sessionid2 = -1;         /*uint32*/
static int hf_trdp_spy_sessionid3 = -1;         /*uint32*/
static int hf_trdp_spy_replytimeout = -1;       /*uint32*/
static int hf_trdp_spy_sourceURI = -1;          /*string*/
static int hf_trdp_spy_destinationURI = -1;     /*string*/
static int hf_trdp_spy_isMD     = -1;           /*flag*/

/* Needed for dynamic content (Generated from convert_proto_tree_add_text.pl) */
static int hf_trdp_spy_dataset_id = -1;

static gboolean preference_changed = TRUE;

static const true_false_string true_false = { "True", "False" };

/* Global sample preference ("controls" display of numbers) */
static const char *gbl_trdpDictionary_1 = NULL; //XML Config Files String from ..Edit/Preference menu
static guint g_pd_port = TRDP_DEFAULT_UDP_PD_PORT;
static guint g_md_port = TRDP_DEFAULT_UDPTCP_MD_PORT;
static gboolean g_scaled = true;

/* Initialize the subtree pointers */
static gint ett_trdp_spy = -1;

/* Expert fields */
static expert_field ei_trdp_type_unkown = EI_INIT;
static expert_field ei_trdp_packet_small = EI_INIT;
static expert_field ei_trdp_userdata_empty = EI_INIT;
static expert_field ei_trdp_userdata_wrong = EI_INIT;
static expert_field ei_trdp_config_notparsed = EI_INIT;
static expert_field ei_trdp_padding_not_zero = EI_INIT;
static expert_field ei_trdp_array_wrong = EI_INIT;


struct {
    wmem_array_t* hf;
    wmem_array_t* ett;
} trdp_build_dict;


#ifdef __cplusplus
extern "C" {
#endif


static TrdpConfigHandler *pTrdpParser = NULL;

/******************************************************************************
 * Local Functions
 */

/**
 * @internal
 * Compares the found CRC in a package with a calculated version
 *
 * @param tvb           dissected package
 * @param trdp_spy_tree tree, where the information will be added as child
 * @param ref_fcs       name of the global dissect variable
 * @param ref_fcs_calc      name of the global dissect variable, when the calculation failed
 * @param offset        the offset in the package where the (32bit) CRC is stored
 * @param data_start    start where the data begins, the CRC should be calculated from
 * @param data_end      end where the data stops, the CRC should be calculated from
 * @param descr_text    description (normally "Header" or "Userdata")
 */
static void add_crc2tree(tvbuff_t *tvb, proto_tree *trdp_spy_tree, int ref_fcs _U_, int ref_fcs_calc _U_, guint32 offset, guint32 data_start, guint32 data_end, const char* descr_text)
{
	guint32 calced_crc, package_crc, length;
	guint8* pBuff;

	// this must always fit (if not, the programmer made a big mistake -> display nothing)
	if (data_start > data_end) {
		return;
	}

	length = data_end - data_start;

	pBuff = (guint8*) g_malloc(length);
	if (pBuff == NULL) { // big problem, could not allocate the needed memory
		return;
	}

	tvb_memcpy(tvb, pBuff, data_start, length);
	calced_crc = g_ntohl(trdp_fcs32(pBuff, length,0xffffffff));

	package_crc = tvb_get_ntohl(tvb, offset);

	if (package_crc == calced_crc)
	{
		proto_tree_add_uint_format_value(trdp_spy_tree, hf_trdp_spy_fcs_head, tvb, offset, 4, package_crc, "%sCrc: 0x%04x [correct]", descr_text, package_crc);

	}
	else
	{
		proto_tree_add_uint_format_value(trdp_spy_tree, hf_trdp_spy_fcs_head_calc, tvb, offset, 4, package_crc, "%s Crc: 0x%04x [incorrect, should be 0x%04x]",
				descr_text, package_crc, calced_crc);

	}
	g_free(pBuff);
}

/* @fn *static void checkPaddingAndOffset(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, guint32 start_offset, guint32 offset)
 *
 * @brief Check for correct padding and calculate the CRC checksum
 *
 * @param[in]   tvb     Buffer with the captured data
 * @param[in]   pinfo   Necessary to mark status of this packet
 * @param[in]   tree    The information is appended
 * @param[in]   start_offset    Beginning of the user data, that is secured by the CRC
 * @param[in]   offset  Actual offset where the padding starts
 *
 * @return position in the buffer after the body CRC
 */
static gint32 checkPaddingAndOffset(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, guint32 start_offset _U_, guint32 offset)
{
	gint32 remainingBytes;
	gint32 isPaddingZero;
	gint32 i;

	/* Jump to the last 4 byte and check the crc */
	remainingBytes = tvb_reported_length_remaining(tvb, offset);
	PRNT2(fprintf(stderr, "The remaining bytes are %d (startoffset=%d, padding=%d)\n", remainingBytes, start_offset, (remainingBytes % 4)));

	if (remainingBytes < 0) /* There is no space for user data */
	{
		return offset;
	}
	else if (remainingBytes > 0)
	{
		isPaddingZero = 0; // flag, if all padding bytes are zero
		for(i = 0; i < remainingBytes; i++)
		{
			if (tvb_get_guint8(tvb, offset + i) != 0)
			{
				isPaddingZero = 1;
				break;
			}
		}
		proto_tree_add_bytes_format_value(tree, hf_trdp_spy_padding, tvb, offset, remainingBytes, NULL, "%s", ( (isPaddingZero == 0) ? "padding" : "padding not zero"));
		offset += remainingBytes;

		/* Mark this packet in the statistics also as "not perfect" */
		if (isPaddingZero != 0)
		{
			expert_add_info_format(pinfo, tree, &ei_trdp_padding_not_zero, "Padding not zero");
		}
	}



	return remainingBytes + TRDP_FCS_LENGTH;
}

/** @fn guint32 dissect_trdp_generic_body(tvbuff_t *tvb, packet_info *pinfo, proto_tree *trdp_spy_tree, proto_tree *trdpRootNode, guint32 trdp_spy_comid, guint32 offset, guint length, guint8 dataset_level, const gchar *title)
 *
 * @brief
 * Extract all information from the userdata (uses the parsebody module for unmarshalling)
 *
 * @param tvb               buffer
 * @param packet            info for the packet
 * @param tree              to which the information are added
 * @param trdpRootNode      Root node of the view of an TRDP packet (Necessary, as this function will be called recursively)
 * @param trdp_spy_comid    the already extracted comId
 * @param offset            where the userdata starts in the TRDP packet
 * @param length            Amount of bytes, that are transported for the users
 * @param dataset_level     is set to 0 for the beginning
 * @param title             presents the instance-name of the dataset
 *
 * @return the actual offset in the packet
 */
static guint32 dissect_trdp_generic_body(tvbuff_t *tvb, packet_info *pinfo, proto_tree *trdp_spy_tree, proto_tree *trdpRootNode, guint32 trdp_spy_comid, guint32 offset, guint clength, guint8 dataset_level, const gchar *title, const gint32 arr_idx )
{
	guint32 start_offset = offset; /* mark the beginning of the userdata in the package */
	gint length;
	const Dataset* ds     = NULL;
	proto_tree *trdp_spy_userdata   = NULL;
	proto_tree *userdata_element = NULL;
	proto_item *pi              = NULL;
	gint array_index;
	gint element_count = 0;
	gdouble formated_value;

	API_TRACE;

	/* make the userdata accessible for wireshark */
	if (!dataset_level) {
		if (!clength) {
			return checkPaddingAndOffset(tvb, pinfo, trdp_spy_tree, start_offset, offset);
		}

		pi = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_userdata, tvb, offset, clength, FALSE);

		if (!pTrdpParser || !pTrdpParser->isInitialized() ) {
			offset += clength;
			if (pTrdpParser) expert_add_info_format(pinfo, trdp_spy_tree, &ei_trdp_config_notparsed, "Configuration could not be parsed.");
			PRNT(fprintf(stderr, "TRDP | Configuration could not be parsed.\n"));
			return checkPaddingAndOffset(tvb, pinfo, trdp_spy_tree, start_offset, offset);
		}

		PRNT(fprintf(stderr, "Searching for comid %d\n", trdp_spy_comid));
		ds = pTrdpParser->const_search(trdp_spy_comid);

		/* so far, length was all userdata received, but this is not true for sub-datasets. */
		/* but here we can check it works out */
		length = ds ? ds->getSize() : -1;

		if (length < 0) { /* No valid configuration for this ComId available */
			/* Move position in front of CRC (padding included) */
			proto_tree_add_expert_format(trdp_spy_userdata, pinfo, &ei_trdp_userdata_empty, tvb, offset, clength, "Userdata should be empty or was incomplete, cannot parse. Check xml-config.");
			PRNT(fprintf(stderr, "No Dataset, %d byte of userdata -> end offset is %d [dataset-level: %d]\n", clength, offset, dataset_level));
			offset += clength;
			return checkPaddingAndOffset(tvb, pinfo, trdp_spy_tree, start_offset, offset);
		}
	} else {

		if (pTrdpParser) {
			PRNT(fprintf(stderr, "Searching for dataset %d\n", trdp_spy_comid));
			ds = pTrdpParser->const_searchDataset(trdp_spy_comid /* <- datasetID */);
		}
		length = ds ? ds->getSize() : -1;
		if (length < 0) { /* this should actually not happen, ie. should be caught in initial comID-round */
			proto_tree_add_expert_format(trdp_spy_userdata, pinfo, &ei_trdp_userdata_empty, tvb, offset, length, "Userdata should be empty or was incomplete, cannot parse. Check xml-config.");
			return offset;
		}
	}

	PRNT(fprintf(stderr, "%s aka %d ([%d] octets)\n", ds->getName(), ds->datasetId, length));
	trdp_spy_userdata = (arr_idx >= 0) ?
		  proto_tree_add_subtree_format(trdp_spy_tree, tvb, offset, length ? length : -1, ds->ett_id, &pi, "%s.%d", title, arr_idx)
		: proto_tree_add_subtree_format(trdp_spy_tree, tvb, offset, length ? length : -1, ds->ett_id, &pi, "%s (%d): %s", ds->getName(), ds->datasetId, title);

	array_index = 0;
	formated_value = 0;
	gint potential_array_size = -1;
	for (Element *el = ds->listOfElements; el; el=el->next) {

		PRNT(fprintf(stderr, "[%d] Offset %5d ----> Element: type=%2d %s\tname=%s\tarray-size=%d\tunit=%s\tscale=%f\toffset=%d\n", dataset_level,
				offset, el->getType(), el->getTypeName(), el->getName(), el->array_size, el->getUnit(), el->scale, el->offset));

		// at startup of a new item, check if it is an array or not
		gint remainder = 0;
		element_count = el->array_size;

		if (!element_count) {// handle variable element count

			if (el->type == TRDP_CHAR8 || el->type == TRDP_UTF16)	{ /* handle the special elements CHAR8 and UTF16: */

				/* Store the maximum possible length for the dynamic datastructure */
				//remainder = tvb_reported_length_remaining(tvb, offset) - TRDP_FCS_LENGTH;

				/* Search for a zero to determine the end of the dynamic length field */
				//for(; element_count < remainder && tvb_get_guint8(tvb, offset + element_count); element_count++);
				//element_count++; /* include the terminator into the element */

				//proto_tree_add_bytes_format_value(trdp_spy_userdata, hf_trdp_ds_type2and3, tvb, offset, length, NULL, "%s [%d]", el->name.toLatin1().data() , element_count);
			} else {
				element_count = potential_array_size;

				if (element_count < 1) {
					expert_add_info_format(pinfo, trdp_spy_tree, &ei_trdp_array_wrong, "%s : was introduced by an unsupported length field. (%d)", el->getName(), potential_array_size);
					element_count = 0;
					potential_array_size = -1;
					continue; /* if, at the end of the day, the array is intentinally 0, skip the element */
				} else {
					PRNT(fprintf(stderr, "[%d] Offset %5d Dynamic array, with %d elements found\n", dataset_level, offset, element_count));
				}

				// check if the specified amount could be found in the package
				remainder = tvb_reported_length_remaining(tvb, offset + el->calculateSize(element_count));
				if (remainder > 0 && remainder < TRDP_FCS_LENGTH /* check space for FCS */) {
					expert_add_info_format(pinfo, trdp_spy_tree, &ei_trdp_userdata_wrong, "%s : has %d elements [%d byte each], but only %d left",
							el->getName(), element_count, el->calculateSize(), tvb_reported_length_remaining(tvb, offset));
					element_count = 0;
				}
			}
		}
		if (element_count > 1) {
			PRNT(fprintf(stderr, "[%d] Offset %5d -- Array found, expecting %d elements using %d bytes\n", dataset_level, offset, element_count, el->calculateSize(element_count)));
		}

		// For an array, inject a new node in the graphical dissector, tree (also the extracted dynamic information, see above are added)
		userdata_element = ((element_count == 1) || (el->type == TRDP_CHAR8) || (el->type == TRDP_UTF16)) /* for single line */
				? trdp_spy_userdata                                                                     /* take existing branch */
				: proto_tree_add_subtree_format(trdp_spy_userdata, tvb, offset, el->calculateSize(element_count), el->ett_id, &pi, "%s (%d) : %s[%d]", el->getTypeName(), el->getType(), el->getName(), element_count);

		do {
			gint64  vals = 0;
			guint64 valu = 0;
			gchar  *text = NULL;
			guint   slen = 0;
			gdouble real64 = 0;
			GTimeVal time = {0,0};
			nstime_t nstime = {0,0};

			switch(el->type) {

			case TRDP_BOOL8: //    1
				valu = tvb_get_guint8(tvb, offset);
				proto_tree_add_boolean(userdata_element, el->hf_id, tvb, offset, el->width, valu);
				offset += el->width;
				break;
			case TRDP_CHAR8:
				if (!element_count) {
					slen = tvb_strsize(tvb, offset);
//					text = tvb_format_stringzpad(tvb, offset, slen);
				} else {
					slen = element_count;
//					text = tvb_format_text(tvb, offset, slen);
				}
				text = tvb_format_text(tvb, offset, slen);
				if (element_count == 1)
					proto_tree_add_string_format_value(userdata_element, el->hf_id, tvb, offset, slen, text, "%s : %s", el->getName(), text);
				else
					proto_tree_add_string_format_value(userdata_element, el->hf_id,  tvb, offset, slen, text, "%s : [%d]\"%s\"", el->getName(), slen, text);
				offset += slen;
				element_count = 1;
				break;
			case TRDP_UTF16:
				if (!element_count) {
					slen = tvb_unicode_strsize(tvb, offset);
//					text = tvb_format_stringzpad(tvb, offset, slen);
				} else {
					slen = 2*element_count; // I interpret (!) for the array-size to be the number of elements, characters, c16.
//					text = tvb_format_text(tvb, offset, slen);
				}
				text = tvb_format_text(tvb, offset, slen);
				proto_tree_add_string_format_value(userdata_element, el->hf_id, tvb, offset, slen, text, "%s : [%d]\"%s\"", el->getName(), slen/2, text);
				offset += slen;
				element_count = 1;
				break;
			case TRDP_INT8:
				vals = tvb_get_gint8(tvb, offset);
				break;
			case TRDP_INT16:
				vals = tvb_get_ntohis(tvb, offset);
				break;
			case TRDP_INT32:
				vals = tvb_get_ntohil(tvb, offset);
				break;
			case TRDP_INT64:
				vals = tvb_get_ntohi64(tvb, offset);
				break;
			case TRDP_UINT8:
				valu = tvb_get_guint8(tvb, offset);
				break;
			case TRDP_UINT16:
				valu = tvb_get_ntohs(tvb, offset);
				break;
			case TRDP_UINT32:
				valu = tvb_get_ntohl(tvb, offset);
				break;
			case TRDP_UINT64:
				valu = tvb_get_ntoh64(tvb, offset);
				break;
			case TRDP_REAL32:
				real64 = tvb_get_ntohieee_float(tvb, offset);
				break;
			case TRDP_REAL64:
				real64 = tvb_get_ntohieee_double(tvb, offset);
				break;
			case TRDP_TIMEDATE32:
				valu = tvb_get_ntohl(tvb, offset);
				time.tv_sec = valu;
				nstime.secs = valu;
				break;
			case TRDP_TIMEDATE48:
				valu = tvb_get_ntohl(tvb, offset);
				time.tv_sec = valu;
				nstime.secs = valu;
				valu = tvb_get_ntohs(tvb, offset + 4);
				nstime.nsecs = valu*(1000000000L/15259L);  // TODO how are ticks calculated to microseconds?
				time.tv_usec = nstime.nsecs/1000;
				break;
			case TRDP_TIMEDATE64:
				valu = tvb_get_ntohl(tvb, offset);
				time.tv_sec = valu;
				nstime.secs = valu;
				valu = tvb_get_ntohl(tvb, offset + 4);
				nstime.nsecs = valu*1000;
				time.tv_usec = valu;
				break;
			default:
				PRNT(fprintf(stderr, "Unique type %d for %s\n", el->type, el->getName()));
				offset = dissect_trdp_generic_body(
						tvb, pinfo, userdata_element, trdpRootNode, el->type, offset, length-(offset-start_offset), dataset_level+1, el->getName(), (element_count != 1) ? array_index : -1);
				break;
			}


			switch (el->type) {
//			case TRDP_INT8 ... TRDP_INT64:
			case TRDP_INT8:
			case TRDP_INT16:
			case TRDP_INT32:
			case TRDP_INT64:

				if (el->scale && g_scaled) {
					formated_value = vals * el->scale + el->offset;
					proto_tree_add_double_format_value(userdata_element, el->hf_id, tvb, offset, el->width, formated_value, "%lg %s (raw=%" G_GINT64_FORMAT ")", formated_value, el->getUnit(), vals);
				} else {
					if (g_scaled) vals += el->offset;
					proto_tree_add_int64(userdata_element, el->hf_id, tvb, offset, el->width, vals);
				}
				offset += el->width;
				break;
//			case TRDP_UINT8 ... TRDP_UINT64:
			case TRDP_UINT8:
			case TRDP_UINT16:
			case TRDP_UINT32:
			case TRDP_UINT64:
				if (el->scale && g_scaled) {
					formated_value = valu * el->scale + el->offset;
					proto_tree_add_double_format_value(userdata_element, el->hf_id, tvb, offset, el->width, formated_value, "%lg %s (raw=%" G_GUINT64_FORMAT ")", formated_value, el->getUnit(), valu);
				} else {
					if (g_scaled) valu += el->offset;
					proto_tree_add_uint64(userdata_element, el->hf_id, tvb, offset, el->width, valu);
				}
				offset += el->width;
				break;
			case TRDP_REAL32:
			case TRDP_REAL64:
				if (el->scale && g_scaled) {
					formated_value = real64 * el->scale + el->offset;
					proto_tree_add_double_format_value(userdata_element, el->hf_id, tvb, offset, el->width, formated_value, "%lg %s (raw=%lf)", formated_value, el->getUnit(), real64);
				} else {
					if (g_scaled) real64 += el->offset;
					proto_tree_add_double(userdata_element, el->hf_id, tvb, offset, el->width, real64);
				}
				offset += el->width;
				break;
//			case TRDP_TIMEDATE32 ... TRDP_TIMEDATE64:
			case TRDP_TIMEDATE32:
			case TRDP_TIMEDATE48:
			case TRDP_TIMEDATE64:
				//proto_tree_add_bytes_format_value(userdata_element, hf_trdp_ds_type16, tvb, offset, 8, NULL, "%s : %s %s", el->name.toLatin1().data(), g_time_val_to_iso8601(&time), (el->unit.length() != 0) ? el->unit.toLatin1().data() : "");
				proto_tree_add_time_format_value(userdata_element, el->hf_id, tvb, offset, el->width, &nstime, "%s : %s %s", el->getName(), g_time_val_to_iso8601(&time), el->getUnit());
				offset += el->width;
				break;
			}

			if (array_index || element_count != 1) {
				// handle arrays
				PRNT(fprintf(stderr, "[%d / %d]\n", array_index, element_count));
				if (++array_index >= element_count) {
					array_index = 0;
					userdata_element = trdp_spy_userdata;
				}
				potential_array_size = -1;
			} else {
				PRNT(fprintf(stderr, "[%d / %d], (type=%d) val-u=%" G_GUINT64_FORMAT " val-s=%" G_GINT64_FORMAT ".\n", array_index, element_count, el->type, valu, vals));

				potential_array_size = (el->type < TRDP_INT8 || el->type > TRDP_UINT64) ? -1 : (el->type >= TRDP_UINT8 ? valu : vals);
			}

		} while(array_index);

	}

	/* Check padding and CRC of the body */
	if (!dataset_level)
		offset = checkPaddingAndOffset(tvb, pinfo, trdpRootNode, start_offset, offset);

	return offset;
}

static void add_dataset_reg_info(Dataset *ds);
static void register_trdp_fields(const char* unused _U_);

/**
 * @internal
 * Extract all information from the userdata (uses the parsebody module for unmarshalling)
 *
 * @param tvb               buffer
 * @param packet            info for tht packet
 * @param tree              to which the information are added
 * @param trdp_spy_comid    the already extracted comId
 * @param offset            where the userdata starts in the TRDP package
 *
 * @return nothing
 */
static guint32 dissect_trdp_body(tvbuff_t *tvb, packet_info *pinfo, proto_tree *trdp_spy_tree, guint32 trdp_spy_comid, guint32 offset, guint32 length)
{
	API_TRACE;

	return dissect_trdp_generic_body(tvb, pinfo, trdp_spy_tree, trdp_spy_tree, trdp_spy_comid, offset, length, 0/* level of cascaded datasets*/, "", -1);
}

/**
 * @internal
 * Build the special header for PD and MD datasets
 * (and calls the function to extract the userdata)
 *
 * @param tvb               buffer
 * @param pinfo             info for tht packet
 * @param tree              to which the information are added
 * @param trdp_spy_comid    the already extracted comId
 * @param offset            where the userdata starts in the TRDP package
 *
 * @return nothing
 */
static guint32 build_trdp_tree(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, proto_item **ti_type,
		guint32 trdp_spy_comid, gchar* trdp_spy_string)
{
	proto_item *ti              = NULL;
	proto_tree *trdp_spy_tree   = NULL;
	proto_item *_ti_type_tmp    = NULL;
	proto_item **pti_type       = ti_type ? ti_type : &_ti_type_tmp;

	guint32 datasetlength = 0;
	guint32 pdu_size = 0;

	API_TRACE;

	/* load the trdp and pdu configuration */
	if (preference_changed) {
		API_TRACE;
		if (pTrdpParser) { /* first need to clean up the dictionary */
			API_TRACE;
			delete pTrdpParser;
			pTrdpParser = NULL;
			proto_free_deregistered_fields();
		}
	    proto_register_prefix("trdp", register_trdp_fields); // re-register to trigger dictionary reading
		proto_registrar_get_byname("trdp.pdu");
	}

	// when the package is big enough exract some data.
	if (tvb_reported_length_remaining(tvb, 0) > TRDP_HEADER_PD_OFFSET_RESERVED)
	{
		ti = proto_tree_add_item(tree, proto_trdp_spy, tvb, 0, -1, ENC_NA);
		trdp_spy_tree = proto_item_add_subtree(ti, ett_trdp_spy);

		ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_sequencecounter, tvb, TRDP_HEADER_OFFSET_SEQCNT, 4, FALSE);
		int verMain = tvb_get_guint8(tvb, TRDP_HEADER_OFFSET_PROTOVER);
		int verSub  = tvb_get_guint8(tvb, (TRDP_HEADER_OFFSET_PROTOVER + 1));
		ti = proto_tree_add_bytes_format_value(trdp_spy_tree, hf_trdp_spy_protocolversion, tvb, 4, 2, NULL, "Protocol Version: %d.%d", verMain, verSub);
		*pti_type = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_type, tvb, TRDP_HEADER_OFFSET_TYPE, 2, FALSE);
		ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_comid, tvb, TRDP_HEADER_OFFSET_COMID, 4, FALSE);
		ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_etb_topocount, tvb, TRDP_HEADER_OFFSET_ETB_TOPOCNT, 4, FALSE);
		ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_op_trn_topocount, tvb, TRDP_HEADER_OFFSET_OP_TRN_TOPOCNT, 4, FALSE);
		ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_datasetlength, tvb, TRDP_HEADER_OFFSET_DATASETLENGTH, 4, FALSE);
		datasetlength = tvb_get_ntohl(tvb, TRDP_HEADER_OFFSET_DATASETLENGTH);
	}
	else
	{
		expert_add_info_format(pinfo, tree, &ei_trdp_packet_small, "Packet too small for header information");
	}

	if (trdp_spy_string)
	{
		switch (trdp_spy_string[0])
		{
		case 'P':
			/* PD specific stuff */
			proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_reserved, tvb, TRDP_HEADER_PD_OFFSET_RESERVED, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_reply_comid, tvb, TRDP_HEADER_PD_OFFSET_REPLY_COMID, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_reply_ipaddress, tvb, TRDP_HEADER_PD_OFFSET_REPLY_IPADDR, 4, FALSE);
			add_crc2tree(tvb,trdp_spy_tree, hf_trdp_spy_fcs_head, hf_trdp_spy_fcs_head_calc, TRDP_HEADER_PD_OFFSET_FCSHEAD , 0, TRDP_HEADER_PD_OFFSET_FCSHEAD, "header");
			pdu_size = dissect_trdp_body(tvb, pinfo, trdp_spy_tree, trdp_spy_comid, TRDP_HEADER_PD_OFFSET_DATA, datasetlength);
			break;
		case 'M':
			/* MD specific stuff */
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_replystatus, tvb, TRDP_HEADER_MD_OFFSET_REPLY_STATUS, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_sessionid0, tvb, TRDP_HEADER_MD_SESSIONID0, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_sessionid1, tvb, TRDP_HEADER_MD_SESSIONID1, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_sessionid2, tvb, TRDP_HEADER_MD_SESSIONID2, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_sessionid3, tvb, TRDP_HEADER_MD_SESSIONID3, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_replytimeout, tvb, TRDP_HEADER_MD_REPLY_TIMEOUT, 4, FALSE);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_sourceURI, tvb, TRDP_HEADER_MD_SRC_URI, 32, ENC_ASCII);
			ti = proto_tree_add_item(trdp_spy_tree, hf_trdp_spy_destinationURI, tvb, TRDP_HEADER_MD_DEST_URI, 32, ENC_ASCII);
			add_crc2tree(tvb,trdp_spy_tree, hf_trdp_spy_fcs_head, hf_trdp_spy_fcs_head_calc, TRDP_HEADER_MD_OFFSET_FCSHEAD, 0, TRDP_HEADER_MD_OFFSET_FCSHEAD, "header");
			pdu_size = dissect_trdp_body(tvb, pinfo, trdp_spy_tree, trdp_spy_comid, TRDP_HEADER_MD_OFFSET_DATA, datasetlength);
			break;
		default:
			break;
		}
	}
	return pdu_size;
}

int dissect_trdp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data _U_)
{
	guint32 trdp_spy_comid = 0;
	gchar* trdp_spy_string = NULL;
	guint32 parsed_size = 0U;
	proto_item *ti_type = NULL;

	API_TRACE;

	/* Make entries in Protocol column ... */
	if (col_get_writable(pinfo->cinfo, COL_PROTOCOL))
	{
		col_set_str(pinfo->cinfo, COL_PROTOCOL, PROTO_TAG_TRDP);
	}

	/* and "info" column on summary display */
	if (col_get_writable(pinfo->cinfo, COL_INFO))
	{
		col_clear(pinfo->cinfo, COL_INFO);
	}

	// Read required values from the package:
	trdp_spy_string = (gchar *) tvb_format_text(tvb, TRDP_HEADER_OFFSET_TYPE, 2);
	trdp_spy_comid = tvb_get_ntohl(tvb, TRDP_HEADER_OFFSET_COMID);

	/* Telegram that fits into one packet, or the header of huge telegram, that was reassembled */
	if (tree != NULL)
	{
		parsed_size = build_trdp_tree(tvb,pinfo,tree, &ti_type, trdp_spy_comid, trdp_spy_string);
	}

	/* Append the packet type into the information description */
	if (col_get_writable(pinfo->cinfo, COL_INFO))
	{
		/* Display a info line */
		col_append_fstr(pinfo->cinfo, COL_INFO, "comId: %5d ",trdp_spy_comid);

		if ((!strcmp(trdp_spy_string,"Pr")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "PD Request");
		}
		else if ((!strcmp(trdp_spy_string,"Pp")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "PD Reply  ");
		}
		else if ((!strcmp(trdp_spy_string,"Pd")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "PD Data   ");
		}
		else if ((!strcmp(trdp_spy_string,"Mn")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "MD Notification (Request without reply)");
		}
		else if ((!strcmp(trdp_spy_string,"Mr")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "MD Request with reply");
		}
		else if ((!strcmp(trdp_spy_string,"Mp")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "MD Reply (without confirmation)");
		}
		else if ((!strcmp(trdp_spy_string,"Mq")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "MD Reply (with confirmation)");
		}
		else if ((!strcmp(trdp_spy_string,"Mc")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "MD Confirm");
		}
		else if ((!strcmp(trdp_spy_string,"Me")))
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "MD error  ");
		}
		else
		{
			col_append_fstr(pinfo->cinfo, COL_INFO, "Unknown TRDP Type");
			expert_add_info_format(pinfo, ti_type, &ei_trdp_type_unkown, "Unknown TRDP Type: %s", trdp_spy_string);
		}

		/* Help with high-level name of ComId / Dataset */
		if (pTrdpParser && pTrdpParser->isInitialized()) {
			const ComId *comId = pTrdpParser->const_searchComId(trdp_spy_comid);
			if (comId) {
				if (*comId->getName() ) {
					col_append_fstr(pinfo->cinfo, COL_INFO, " -> %s", comId->getName());
				} else if (comId->linkedDS && *comId->linkedDS->getName()) {
					col_append_fstr(pinfo->cinfo, COL_INFO, " -> [%s]", comId->linkedDS->getName());
				}
			}
		}
	}
	return parsed_size;
}

/* determine PDU length of protocol foo */

/** @fn static guint get_trdp_tcp_message_len(packet_info *pinfo _U_, tvbuff_t *tvb, int offset)
 * @internal
 * @brief retrieve the expected size of the transmitted packet.
 */
//static guint get_trdp_tcp_message_len(packet_info *pinfo _U_, tvbuff_t *tvb, int offset)
//{
//    guint datasetlength = (guint) tvb_get_ntohl(tvb, offset+16);
//    return datasetlength + TRDP_MD_HEADERLENGTH + TRDP_FCS_LENGTH /* add padding, FIXME must be calculated */;
//}

/**
 * @internal
 * Code to analyze the actual TRDP packet, transmitted via TCP
 *
 * @param tvb       buffer
 * @param pinfo     info for the packet
 * @param tree      to which the information are added
 * @param data      Collected information
 *
 * @return nothing
 */
//static void dissect_trdp_tcp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
//{
//    /*FIXME tcp_dissect_pdus(tvb, pinfo, tree, TRUE, TRDP_MD_HEADERLENGTH,
//                     get_trdp_tcp_message_len, dissect_trdp);*/
//}

void proto_reg_handoff_trdp(void)
{
	static gboolean inited = FALSE;
	static dissector_handle_t trdp_spy_handle;
	//    static dissector_handle_t trdp_spy_TCP_handle;

	preference_changed = TRUE;

	API_TRACE;

	if(!inited )
	{
		trdp_spy_handle     = create_dissector_handle((dissector_t) dissect_trdp, proto_trdp_spy);
		//FIXME trdp_spy_TCP_handle = create_dissector_handle((dissector_t) dissect_trdp_tcp, proto_trdp_spy_TCP);
		inited = TRUE;
	}
	else
	{
		dissector_delete_uint("udp.port", g_pd_port, trdp_spy_handle);
		dissector_delete_uint("udp.port", g_md_port, trdp_spy_handle);
		//        dissector_delete_uint("tcp.port", g_md_port, trdp_spy_TCP_handle);
	}
	dissector_add_uint("udp.port", g_pd_port, trdp_spy_handle);
	dissector_add_uint("udp.port", g_md_port, trdp_spy_handle);
	//    dissector_add_uint("tcp.port", g_md_port, trdp_spy_TCP_handle);
}

/* ========================================================================= */
/* Register the protocol fields and subtrees with Wireshark (strongly inspired by the wimaxasncp plugin)*/

/* ========================================================================= */
/* Modify the given string to make a suitable display filter                 */
/*                                             copied from wimaxasncp plugin */
static char *alnumerize(char *name) {
	char *r = name;  /* read pointer */
	char *w = name;  /* write pointer */
	char  c;

	for ( ; (c = *r); ++r) {
		if (g_ascii_isalnum(c) || c == '_' || c == '.') { /* These characters are fine - copy them */
			*(w++) = c;
		} else if (c == ' ' || c == '-' || c == '/') {
			if (w == name) continue;                      /* Skip these others if haven't written any characters out yet */

			if (*(w - 1) == '_') continue;                /* Skip if we would produce multiple adjacent '_'s */

			*(w++) = '_';                                 /* OK, replace with underscore */
		}
		/* Other undesirable characters are just skipped */
	}
	*w = '\0';                                            /* Terminate and return modified string */
	return name;
}

/* ========================================================================= */

static void add_reg_info(gint *hf_ptr, const char *name, const char *abbrev, enum ftenum type, gint display, const char  *blurb) {
	hf_register_info hf = {
			hf_ptr, { name, abbrev, type, display, NULL, 0, blurb, HFILL } };

	wmem_array_append_one(trdp_build_dict.hf, hf);
}

/* ========================================================================= */

static void add_element_reg_info(const char *parentName, Element *el) {
	char *name;
	char *abbrev;
	const char *blurb;
	gint *pett_id = &el->ett_id;

	//name = wmem_strdup(wmem_epan_scope(), el->getName());
	name = g_strdup(el->getName());
	//abbrev = alnumerize(wmem_strdup_printf(wmem_epan_scope(), "trdp.pdu.%s.%s", parentName, el->getName()));
	abbrev = alnumerize(g_strdup_printf("trdp.pdu.%s.%s", parentName, el->getName()));

	if (el->scale || el->offset) {
		blurb = g_strdup_printf("type=%s(%u) *%4g %+0d %s", el->getTypeName(), el->getType(), el->scale?el->scale:1.0, el->offset, el->getUnit());
	} else {
		blurb = g_strdup_printf("type=%s(%u) %s", el->getTypeName(), el->getType(), el->getUnit());
	}

	if (!((el->array_size == 1) || (el->type == TRDP_CHAR8) || (el->type == TRDP_UTF16))) {
		wmem_array_append_one(trdp_build_dict.ett, pett_id);
	}

	if (el->scale && g_scaled)
		add_reg_info( &el->hf_id, name, abbrev, FT_DOUBLE, BASE_NONE, blurb );
	else
		switch (el->getType()) {
	case TRDP_BOOL8:
		add_reg_info( &el->hf_id, name, abbrev, FT_BOOLEAN, 8, blurb );
		break;
	case TRDP_CHAR8:
		add_reg_info( &el->hf_id, name, abbrev, el->array_size?FT_STRING:FT_STRINGZ, STR_ASCII, blurb );
		break;
	case TRDP_UTF16:
		add_reg_info( &el->hf_id, name, abbrev, el->array_size?FT_STRING:FT_STRINGZ, STR_UNICODE, blurb );
		break;
//	case TRDP_INT8 ... TRDP_INT64: not supported in MSVC :(
	case TRDP_INT8:
	case TRDP_INT16:
	case TRDP_INT32:
	case TRDP_INT64:
		add_reg_info( &el->hf_id, name, abbrev, FT_INT64 , BASE_DEC, blurb );
		break;
//	case TRDP_UINT8 ... TRDP_UINT64:
	case TRDP_UINT8:
	case TRDP_UINT16:
	case TRDP_UINT32:
	case TRDP_UINT64:
		add_reg_info( &el->hf_id, name, abbrev, FT_UINT64, BASE_DEC, blurb );
		break;
	case TRDP_REAL32:
	case TRDP_REAL64:
		add_reg_info( &el->hf_id, name, abbrev, FT_DOUBLE, BASE_NONE, blurb );
		break;
//	case TRDP_TIMEDATE32 ... TRDP_TIMEDATE64:
	case TRDP_TIMEDATE32:
	case TRDP_TIMEDATE48:
	case TRDP_TIMEDATE64:
		add_reg_info( &el->hf_id, name, abbrev, FT_ABSOLUTE_TIME, ABSOLUTE_TIME_LOCAL, blurb );
		break;
	default:
		add_reg_info( &el->hf_id, name, abbrev, FT_BYTES, BASE_NONE, blurb );
		/* as long as I do not track the hierarchy, do not recurse */
		// add_dataset_reg_info(el->linkedDS);
	}
}

static void add_dataset_reg_info(Dataset *ds) {
	gint *pett_id = &ds->ett_id;
	for(Element *el = ds->listOfElements; el; el=el->next) {
		add_element_reg_info(ds->getName(), el);
	}
	if (ds->listOfElements)
		wmem_array_append_one(trdp_build_dict.ett, pett_id);
}


static void
register_trdp_fields(const char* unused _U_) {
	API_TRACE;
	/* List of header fields. */
	static hf_register_info hf_base[] ={
			/* All the general fields for the header */
			{ &hf_trdp_spy_sequencecounter,      { "sequenceCounter",      "trdp.sequencecounter",     FT_UINT32, BASE_DEC, NULL,   0x0, "", HFILL } },
			{ &hf_trdp_spy_protocolversion,      { "protocolVersion", "trdp.protocolversion", FT_BYTES, BASE_NONE, NULL, 0x0, "", HFILL } },
			{ &hf_trdp_spy_type,                 { "msgtype",              "trdp.type",                FT_STRING, ENC_NA | ENC_ASCII, NULL, 0x0, "", HFILL } },
			{ &hf_trdp_spy_comid,                { "comId",                "trdp.comid",               FT_UINT32, BASE_DEC, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_etb_topocount,        { "etbTopoCnt",        "trdp.etbtopocnt",          FT_UINT32, BASE_DEC, NULL,   0x0, "", HFILL } },
			{ &hf_trdp_spy_op_trn_topocount,     { "opTrnTopoCnt",     "trdp.optrntopocnt",        FT_UINT32, BASE_DEC, NULL,   0x0, "", HFILL } },
			{ &hf_trdp_spy_datasetlength,        { "datasetLength",        "trdp.datasetlength",       FT_UINT32, BASE_DEC, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_padding,              { "padding",           "trdp.padding",                 FT_BYTES, BASE_NONE, NULL, 0x0,     "", HFILL } },

			/* PD specific stuff */
			{ &hf_trdp_spy_reserved,            { "reserved",               "trdp.reserved",    FT_UINT32, BASE_DEC, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_reply_comid,         { "replyComId",        "trdp.replycomid",  FT_UINT32, BASE_DEC, NULL, 0x0,     "", HFILL } }, /* only used in a PD request */
			{ &hf_trdp_spy_reply_ipaddress,     { "replyIpAddress",       "trdp.replyip",     FT_IPv4, BASE_NONE, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_isPD,                { "processData",           "trdp.pd",          FT_STRING, BASE_NONE, NULL, 0x0, "", HFILL } },

			/* MD specific stuff */
			{ &hf_trdp_spy_replystatus,     { "replyStatus",  "trdp.replystatus",      FT_UINT32, BASE_DEC, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_sessionid0,      { "sessionId0",  "trdp.sessionid0",   FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_sessionid1,      { "sessionId1",  "trdp.sessionid1",   FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_sessionid2,      { "sessionId2",  "trdp.sessionid2",   FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_sessionid3,      { "sessionId3",  "trdp.sessionid3",   FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_replytimeout,    { "replyTimeout",  "trdp.replytimeout",    FT_UINT32, BASE_DEC, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_sourceURI,       { "sourceUri",  "trdp.sourceUri",          FT_STRING, BASE_NONE, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_destinationURI,  { "destinationURI",  "trdp.destinationUri",    FT_STRING, BASE_NONE, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_isMD,            { "messageData",  "trdp.md",               FT_STRING, BASE_NONE, NULL, 0x0, "", HFILL } },
			{ &hf_trdp_spy_userdata,        { "dataset",   "trdp.rawdata",         FT_BYTES, BASE_NONE, NULL, 0x0,     "", HFILL } },

			/* The checksum for the header (the trdp.fcsheadcalc is only set, if the calculated FCS differs) */
			{ &hf_trdp_spy_fcs_head,             { "headerFcs",                "trdp.fcshead",     FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_fcs_head_calc,        { "calculatedHeaderFcs",     "trdp.fcsheadcalc", FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },
			{ &hf_trdp_spy_fcs_head_data,        { "FCS (DATA)",                "trdp.fcsheaddata", FT_UINT32, BASE_HEX, NULL, 0x0,     "", HFILL } },

			/* Dynamic generated content */
			{ &hf_trdp_spy_dataset_id,          { "Dataset id", "trdp.dataset_id", FT_NONE, BASE_NONE, NULL, 0x0, "", HFILL } },

	};

	/* Setup protocol subtree array */
	static gint *ett_base[] = {
			&ett_trdp_spy,
	};


	/* ------------------------------------------------------------------------
	 * load the XML dictionary
	 * ------------------------------------------------------------------------
	 */

	API_TRACE;
	if ( preference_changed || pTrdpParser == NULL) {
		PRNT2(fprintf(stderr, "TRDP dictionary is '%s' (changed=%d, pTrdpParser=%d)\n", gbl_trdpDictionary_1, preference_changed, !!pTrdpParser));
		if (pTrdpParser != NULL) {
			delete pTrdpParser;
			pTrdpParser = NULL;
			proto_free_deregistered_fields();
		}
		if ( gbl_trdpDictionary_1 && *gbl_trdpDictionary_1 ) { /* keep this silent on no set file */
			API_TRACE;
			pTrdpParser = new TrdpConfigHandler(gbl_trdpDictionary_1, proto_trdp_spy);
		}
		preference_changed = FALSE;
	}

	if (pTrdpParser && !pTrdpParser->isInitialized()) {
		report_failure("trdp - %s", pTrdpParser->getError());
	}

	/* ------------------------------------------------------------------------
	 * build the hf and ett dictionary entries
	 * ------------------------------------------------------------------------
	 */

	if (trdp_build_dict.hf)  wmem_free(wmem_epan_scope(), trdp_build_dict.hf);
	if (trdp_build_dict.ett) wmem_free(wmem_epan_scope(), trdp_build_dict.ett);
	trdp_build_dict.hf  = wmem_array_new(wmem_epan_scope(), sizeof(hf_register_info));
	trdp_build_dict.ett = wmem_array_new(wmem_epan_scope(), sizeof(gint*));

	if (hf_trdp_spy_type == -1) {
		proto_register_field_array(proto_trdp_spy, hf_base, array_length(hf_base));
//		wmem_array_append(trdp_build_dict.hf, hf_base, array_length(hf_base));
		proto_register_subtree_array(ett_base, array_length(ett_base));
//		wmem_array_append(trdp_build_dict.ett, ett_base, array_length(ett_base));
	}

	if (pTrdpParser) {
		/* arrays use the same hf */
		/* don't care about comID linkage, as I really want to index all datasets, regardless of their hierarchy */
		for (Dataset *ds=pTrdpParser->mTableDataset; ds; ds=ds->next)
			add_dataset_reg_info(ds);
	}


	/* Required function calls to register the header fields and subtrees used */

	proto_register_field_array(proto_trdp_spy,
			(hf_register_info*)wmem_array_get_raw(trdp_build_dict.hf),
			wmem_array_get_count(trdp_build_dict.hf));

	proto_register_subtree_array(
			(gint**)wmem_array_get_raw(trdp_build_dict.ett),
			wmem_array_get_count(trdp_build_dict.ett));

}

void proto_register_trdp(void) {
	module_t *trdp_spy_module;

	API_TRACE;

	/* Register the protocol name and description */
	proto_trdp_spy = proto_register_protocol(PROTO_NAME_TRDP, PROTO_TAG_TRDP, PROTO_FILTERNAME_TRDP);

	register_dissector(PROTO_TAG_TRDP, (dissector_t) dissect_trdp, proto_trdp_spy);

	/* Register preferences module (See Section 2.6 for more on preferences) */
	trdp_spy_module = prefs_register_protocol(proto_trdp_spy, proto_reg_handoff_trdp);

	/* Register the preference */
	prefs_register_filename_preference(trdp_spy_module, "configfile",
			"TRDP configuration file",
			"TRDP configuration file",
			&gbl_trdpDictionary_1
#if ((VERSION_MAJOR > 2) || (VERSION_MAJOR == 2 && VERSION_MICRO >= 4))
			, false
#endif
	);
	prefs_register_bool_preference(trdp_spy_module, "scaled",
			"Use scaled value for filter",
			"When ticked, uses scaled values for filtering and display, otherwise the raw value.", &g_scaled);
	prefs_register_uint_preference(trdp_spy_module, "pd.udp.port",
			"PD message Port",
			"UDP port for PD messages (Default port is " TRDP_DEFAULT_STR_PD_PORT ")", 10 /*base */, &g_pd_port);
	prefs_register_uint_preference(trdp_spy_module, "md.udptcp.port",
			"MD message Port",
			"UDP and TCP port for MD messages (Default port is " TRDP_DEFAULT_STR_MD_PORT ")", 10 /*base */, &g_md_port);


	/* Register expert information */
	expert_module_t* expert_trdp;
	static ei_register_info ei[] = {
			{ &ei_trdp_type_unkown, { "trdp.type_unkown", PI_UNDECODED, PI_WARN, "TRDP type unkown", EXPFILL }},
			{ &ei_trdp_packet_small, { "trdp.packet_size", PI_UNDECODED, PI_WARN, "TRDP packet too small", EXPFILL }},
			{ &ei_trdp_userdata_empty, { "trdp.userdata_empty", PI_UNDECODED, PI_WARN, "TRDP user data is empty", EXPFILL }},
			{ &ei_trdp_userdata_wrong , { "trdp.userdata_wrong", PI_UNDECODED, PI_WARN, "TRDP user data has wrong format", EXPFILL }},
			{ &ei_trdp_config_notparsed, { "trdp.config_unparsable", PI_UNDECODED, PI_WARN, "TRDP XML configuration cannot be parsed", EXPFILL }},
			{ &ei_trdp_padding_not_zero, { "trdp.padding", PI_MALFORMED, PI_WARN, "TRDP Padding not filled with zero", EXPFILL }},
			{ &ei_trdp_array_wrong, { "trdp.array", PI_MALFORMED, PI_WARN, "Dynamic array has unsupported datatype for length", EXPFILL }},
	};

	expert_trdp = expert_register_protocol(proto_trdp_spy);
	expert_register_field_array(expert_trdp, ei, array_length(ei));

    proto_register_prefix("trdp", register_trdp_fields);
}

#ifdef __cplusplus
}
#endif
