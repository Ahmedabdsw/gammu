/**
 * \file gammu-inifile.h
 * \author Michal Čihař
 * 
 * INI files manipulation.
 */
#ifndef __gammu_inifile_h
#define __gammu_inifile_h

#include <gammu-types.h>
/**
 * \defgroup INI INI files
 * Handling of INI files.
 */

/**
 * Private structure holding information INI entry.
 * \ingroup INI
 */
typedef struct _INI_Entry INI_Entry;

/**
 * Private structure holding information INI section.
 * \ingroup INI
 */
typedef struct _INI_Section INI_Section;

/**
 * Free INI data.
 *
 * \ingroup INI
 *
 * \param head INI section data.
 */
void INI_Free(INI_Section *head);

/**
 * Reads INI data.
 *
 * \ingroup INI
 *
 * \param FileName File to read.
 * \param Unicode Whether file shoul be treated like unicode.
 * \param result Pointer where file will be read.
 * 
 * \return Error code
 */
GSM_Error INI_ReadFile(char *FileName, bool Unicode, INI_Section **result);

/**
 * Returns pointer to last INI entry of given section.
 *
 * \ingroup INI
 *
 * \param file_info File data as returned by \ref INI_ReadFile.
 * \param section Section to scan.
 * \param Unicode Whether file is unicode.
 *
 * \return Last entry in section.
 *
 * \bug Unicode should be part of file_info.
 */
INI_Entry 	*INI_FindLastSectionEntry (INI_Section *file_info, unsigned char *section, bool Unicode);

/**
 * Returns value of INI file entry.
 *
 * \ingroup INI
 *
 * \param file_info File data as returned by \ref INI_ReadFile.
 * \param section Section to scan.
 * \param key Name of key to read.
 * \param Unicode Whether file is unicode.
 *
 * \return Entry value.
 *
 * \bug Unicode should be part of file_info.
 */
unsigned char *INI_GetValue(INI_Section *file_info, unsigned char *section, unsigned char *key, bool Unicode);
#endif
