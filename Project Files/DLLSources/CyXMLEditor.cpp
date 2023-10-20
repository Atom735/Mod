
#include "CvGameCoreDLL.h"

#include "CyXMLEditor.h"

#include "tinyxml2.h"
#include "iconv.h"
#include <direct.h>

using namespace tinyxml2;

// list of directories to search when opening files
// when opening a file, directories will be searched in this order
// since this is all about XML files, provide the path into XML
// the code assumes each path ends with / when appending filenames
const TCHAR* CONST_VANILLA_PATH[] = { 
	NULL,          // replaced with path to mod at startup
	"Assets/XML/", // vanilla, path is relative to the exe file

	// warlords and original for BTS
	NULL,
	NULL,

	// append a NULL at the end as this can be used to assert on errors rather than crash
	NULL };

CvString szAutoPath;

CyXMLEditor* CyXMLEditor::m_pInstance = NULL; // singleton declaration

const TCHAR* CONST_ICON_PATHS[][2] = {
	{ "BoolOn"       ,  "Art/Interface/Buttons/Units/Medallion_CanMove.dds" },
	{ "BoolOff"      ,  "Art/Interface/Buttons/Units/Medallion_CantMove.dds" },
	{ "Clone"        ,  "Art/Interface/MainScreen/CityScreen/hurry_commerce.dds" },
	{ "DefaultButton",  "Art/Buttons/Tech_Categorys/NativeCat.dds" },
	{ "DeleteElement", ",Art/Interface/Buttons/Actions_Builds_LeaderHeads_Specialists_Atlas.dds,8,4" },
	{ "DirClosed"    ,  "Art/Interface/Screens/City_Management/ArrowLEFT.dds" },
	{ "DirOpen"      ,  "Art/Interface/Screens/City_Management/ArrowRIGHT.dds" },
	{ "IntUpArrow"   ,  "Art/Interface/Buttons/up_arrow.dds" },
	{ "IntDownArrow" ,  "Art/Interface/Buttons/down_arrow.dds" },
	};

const TCHAR* LANGUAGES[] = {
	"French",
    "German",
    "Italian",
    "Spanish",
	NULL};

static void saveFile(XMLDocument *pDoc, const TCHAR *filename)
{
	CvString szBuffer = CONST_VANILLA_PATH[0];
	szBuffer.append("buffer.xml");

	pDoc->SaveFile(szBuffer.c_str());
	FILE* fpIn  = fopen(szBuffer.c_str(), "rb" );
	FILE* fpOut = fopen(filename, "wb" );

	iconv::utf8toXML(fpIn, fpOut);

	// close streams
	fclose(fpIn);
	fclose(fpOut);

	// delete the temp file
	remove(szBuffer.c_str());
}

const int iNumTXTTags = 4;

const TCHAR* TXT_TAGS[iNumTXTTags] = {
	"Civilopedia",
	"Description",
	"Help",
	"Strategy",
};

const TCHAR* TXT_TAGS_POSTFIX[iNumTXTTags] = {
	"_PEDIA",
	"",
	"_HELP",
	"_STRATEGY",
};

static bool isText(const TCHAR* szTag)
{
	for (int i = 0; i < 4; ++i)
	{
		if (strcmp(szTag, TXT_TAGS[i]) == 0)
		{
			return true;
		}
	}
	return false;
}

static const TCHAR* sortByChildTag(const XMLElement *pElement)
{
	const XMLElement *pChild = pElement->FirstChildElement("Tag");
	return pChild ? pChild->GetText() : NULL;
}

static const TCHAR* sortByValue(const XMLElement *pElement)
{
	return pElement->Value();
}

// insert a new child element alphabetically
static void insertElement(XMLNode *pParent, XMLElement *pChild, const TCHAR* (*func)(const XMLElement *pElement))
{
	const TCHAR* szTag = (*func)(pChild);
	XMLElement* pLoop = pParent->FirstChildElement();
	while (pLoop != NULL && (*func)(pLoop) != NULL && strcmp((*func)(pLoop), szTag) < 0)
	{
		pLoop = pLoop->NextSiblingElement();
	}

	if (pLoop == NULL)
	{
		// tag is after all existing tags. Attach it to the end of the list
		pParent->InsertEndChild(pChild);
	}
	else
	{
		// attach before pLoop
		// since elements are attached after an element, not before, we need the previous element
		pLoop = pLoop->PreviousSiblingElement();
		if (pLoop == NULL)
		{
			// insert as the first element
			pParent->InsertFirstChild(pChild);
		}
		else
		{
			pParent->InsertAfterChild(pLoop, pChild);
		}
	}
}

static float getFloatChild(XMLElement *pElement, const char *tag)
{
	XMLElement *pChild = pElement->FirstChildElement(tag);
	return pChild != NULL ? pChild->FloatText() : 0;
}

static const char* getTextChild(XMLElement *pElement, const char *tag)
{
	XMLElement *pChild = pElement->FirstChildElement(tag);
	return pChild != NULL ? pChild->GetText() : "";
}

static void clone(XMLElement *pOriginal, XMLElement *pClone)
{
	XMLElement *pOrig = pOriginal->FirstChildElement();
	if (pOrig == NULL)
	{
		const TCHAR *szText = pOriginal->GetText();
		if (szText != NULL)
		{
			pClone->SetText(szText);
		}
		return;
	}

	for (; pOrig != NULL; pOrig = pOrig->NextSiblingElement())
	{
		XMLElement *pNew = pClone->GetDocument()->NewElement(pOrig->Value());
		pClone->InsertEndChild(pNew);
		clone(pOrig, pNew);
	}
}

// change a string into a vector of strings seperated by semicolons
// This is useful for reading strings from text xml files
static std::vector<CvWString> split(CvWString szwText)
{
	std::vector<CvWString> vector;

	while (szwText.size() > 0)
	{
		std::basic_string<wchar>::size_type iIndex = szwText.find_first_of(':');
		if (iIndex == std::basic_string<wchar>::npos)
		{
			vector.push_back(szwText);
			break;
		}
		else
		{
			vector.push_back(szwText.substr(0, iIndex));
			szwText.erase(0, iIndex + 1);
		}
	}

	return vector;
}

CyXMLTextString::CyXMLTextString()
	: m_pElement(NULL)
{
}

CyXMLTextString::CyXMLTextString(XMLElement* m_pElement)
	: m_pElement(m_pElement)
{
}

int getNumStrings(const char *pString)
{
	// init to 1 unless empty string
	int iNum = pString != NULL ? 1 : 0;

	for (const char *pChar = pString; pChar != NULL; ++pChar)
	{
		if (*pChar == ':')
		{
			++iNum;
		}
	}

	return iNum;
}

std::wstring CyXMLTextString::getText(bool bMale, bool bPlural) const
{
	if (m_pElement == NULL)
	{
		return L"";
	}

	XMLElement *pText = m_pElement->FirstChildElement("Text");

	if (pText != NULL)
	{
		XMLElement *pGender = m_pElement->FirstChildElement("Gender");
		XMLElement *pPlural = m_pElement->FirstChildElement("Plural");

		// make vectors of each token in each tag
		std::vector<CvWString> vecText   = split(iconv::fromUTF8(pText->GetText()));
		std::vector<CvWString> vecGender = split(iconv::fromUTF8(pGender->GetText()));
		std::vector<CvWString> vecPlural = split(iconv::fromUTF8(pPlural->GetText()));

		// loop based on the shortest number of tokens
		// ignore the leftovers completely
		// they should all have the same length,
		// but this way malformated xml files can't cause a crash
//		unsigned int iMax = std::min(std::min(vecText.size(), vecGender.size()), vecPlural.size());

		unsigned int iMax = vecText.size();


		// loop all strings from Text
		// it's supported to have more strings in Text than in the other tags
		// if that's the case, the first string will be used
		// This is usually used for say singular and plural of the same gender, in which case the gender is written once
		for (unsigned int i = 0; i < iMax; ++i)
		{
			const wchar *pString;

			// check gender
			pString = vecGender.size() > i ? vecGender[i] : vecGender[0];
			if (bMale)
			{
				if (wcscmp(pString, L"Male") != 0) continue;
			}
			else
			{
				if (wcscmp(pString, L"Female") != 0) continue;
			}

			// check plural
			pString = vecPlural.size() > i ? vecPlural[i] : vecPlural[0];
			if (bPlural)
			{
				if (wcscmp(pString, L"1") != 0) continue;
			}
			else
			{
				if (wcscmp(pString, L"0") != 0) continue;
			}

			// located the right one. Return it
			return vecText[i];
		}
	}
	else if (bMale)
	{
		std::vector<CvWString> vecText = split(iconv::fromUTF8(m_pElement->GetText()));
		unsigned int iIndex = bPlural ? 1 : 0;
		if (iIndex < vecText.size())
		{
			return vecText[iIndex];
		}
	}

	return L"";
}

std::wstring CyXMLTextString::getTextFromIndex(int iIndex) const
{
	switch (iIndex)
	{
	case 0: return getText(true , false);
	case 1: return getText(true , true );
	case 2: return getText(false, false);
	case 3: return getText(false, true );
	}

	return L"";
}

std::wstring CyXMLTextString::getNameFromIndex(int iIndex) const
{
	switch (iIndex)
	{
	case 0: return L"Male Singular (use this if gender/plural doesn't apply)";
	case 1: return L"Male Plural (use this if gender doesn't matter)";
	case 2: return L"Female Singular";
	case 3: return L"Female Plural";
	}

	return L"";
}

TextFileStorage::TextFileStorage(const TCHAR* szPath, bool bVanilla)
	: m_szPath(szPath)
	, m_bVanilla(bVanilla)
	, m_bAutogenerated(false)
	, m_Document(new XMLDocument)
{
	XMLError eError = m_Document->LoadFile(szPath);
	if (eError != XML_SUCCESS)
	{
		// file doesn't exist. Make an empty file ready to add strings to, if needed
		m_Document->Clear();
		m_Document->InsertFirstChild(m_Document->NewDeclaration("xml version=\"1.0\" encoding=\"ISO-8859-1\""));

		XMLElement *pRoot = m_Document->NewElement("Civ4GameText");
		pRoot->SetAttribute("xmlns", "http://www.firaxis.com");
		m_Document->InsertEndChild(pRoot);
	}

	const char* pLast = strrchr(szPath, '/');
	if (pLast != NULL && strlen(pLast) > 10 && strncmp(pLast, "/XML_AUTO_", 10) == 0)
	{
		m_bAutogenerated = true;
	}

	// set up all the tags
	initCache();
	
}

TextFileStorage::~TextFileStorage()
{
	SAFE_DELETE(m_Document);
}

void TextFileStorage::initCache()
{
	m_TypeCache.clear();
	m_TypeStrings.clear();

	XMLElement *pText = m_Document->FirstChildElement("Civ4GameText");
	if (pText == NULL)
	{
		FAssertMsg(false, CvString::format("Malformated file: %s", m_szPath.c_str()).c_str());
		return;
	}
	pText = pText->FirstChildElement("TEXT");

	for (;pText != NULL;pText = pText->NextSiblingElement("TEXT"))
	{
		XMLElement *pElement = pText->FirstChildElement("Tag");
		if (pElement == NULL)
		{
			continue;
		}
		const TCHAR *szTag = pElement->GetText();
		if (szTag == NULL)
		{
			continue;
		}
		m_TypeCache[szTag] = pText;
		m_TypeStrings.push_back(szTag);
	}
}

const TCHAR* TextFileStorage::getTag(int i) const
{
	if (i >= 0 && i < (int)m_TypeStrings.size())
	{
		return m_TypeStrings[i];
	}
	return NULL;
}

XMLElement* TextFileStorage::getElement(const TCHAR* szTag) const
{
	ElementCacheType::const_iterator it = m_TypeCache.find(szTag);
	if (it!=m_TypeCache.end())
	{
		return it->second;
	}
	return NULL;
}

// create a new string with the Tag szTag
XMLElement* TextFileStorage::createElement(const TCHAR* szTag)
{
	XMLElement *pRoot = m_Document->FirstChildElement("Civ4GameText");
	XMLElement *pText = m_Document->NewElement("TEXT");
	// add Tag child
	XMLElement *pTag  = m_Document->NewElement("Tag");
	pTag->SetText(szTag);
	pText->InsertFirstChild(pTag);
	// add English child (it has to be there to avoid crashes)
	XMLElement *pEnglish  = m_Document->NewElement("English");
	pEnglish->SetText("To be overwritten");
	pText->InsertAfterChild(pTag, pEnglish);

	CyXMLEditor::getInstance()->addLanguages(pText);

	// insert into the correct location to maintain alphabetical order
	insertElement(pRoot, pText, sortByChildTag);

	// update cache
	initCache();

	// return the string, which is to be overwritten
	return pEnglish;
}

void TextFileStorage::deleteElement(tinyxml2::XMLElement* pElement)
{
	if (m_bVanilla || m_bAutogenerated)
	{
		// don't write to vanilla files
		// autogenerated files will be overwritten later
		return;
	}
	if (m_Document != pElement->GetDocument())
	{
		FAssertMsg(false, "Deleting text entry in wrong file");
		return;
	}
	XMLNode *pParent = pElement->Parent();
	pParent->DeleteChild(pElement);
	initCache();
	save();
}

void TextFileStorage::save()
{
	saveFile(m_Document, m_szPath);
}


FileStorage::FileStorage(const TCHAR* szName, XMLDocument *Document)
{
	m_szName = szName;
	m_Document = Document;
}

xmlFileContainer::xmlFileContainer()
	: m_Description(NULL)
	, m_bIsInMod(false)
{
}

xmlFileContainer::xmlFileContainer(tinyxml2::XMLElement *Description)
	: m_Description(NULL)
	, m_bIsInMod(false)
{
	m_Description = Description;

	FAssert(getDir() != NULL);
	FAssert(getTag() != NULL);

	if (isCombo())
	{
		comboConstructor();
		return;
	}

	FAssert(getRoot() != NULL);
	FAssert(getName() != NULL);

	XMLDocument *pDocument = new XMLDocument;
	XMLDocument *m_Schema  = new XMLDocument;

	CvString szFileName = getDir();
	szFileName.append("/");
	szFileName.append(getName());
	szFileName.append(".xml");

	m_vectorFiles.push_back(FileStorage(szFileName, pDocument));

	m_bIsInMod = openFile(pDocument, szFileName.c_str());

	// open root
	XMLElement* pRoot = pDocument->FirstChildElement(getRoot());
	const char* pSchemaName = pRoot->Attribute("xmlns");
	FAssertMsg(pSchemaName != NULL, CvString::format("Failed to read schema file for %s", szFileName.c_str()));
	pSchemaName += 9; // remove leading "x-schema:" as we do not need that part

	szFileName = getDir();
	szFileName.append("/");
	szFileName.append(pSchemaName);
	openFile(m_Schema, szFileName.c_str());

	m_szSchemaName = szFileName;

	setAllTags();
}

void xmlFileContainer::comboConstructor()
{
	FAssert(getRoot() == NULL);
	FAssert(getName() == NULL);

	XMLElement *pElement = m_Description->FirstChildElement("SubFiles");
	if (pElement == NULL)
	{
		return;
	}


	XMLElement *pLoop = pElement->FirstChildElement("Tag");
	while (pLoop != NULL)
	{
		const char *szName = pLoop->GetText();
		m_TagCache[szName] = m_szTags.size();
		m_szTags.push_back(szName);
		m_ElementCache[szName] = pElement;
		pLoop = pLoop->NextSiblingElement("Tag");
	}

	// sort the elements to make them appear alphabetically
	std::sort(m_szTags.begin(), m_szTags.end());
}

xmlFileContainer::~xmlFileContainer()
{
	while (!m_vectorFiles.empty())
	{
		SAFE_DELETE(m_vectorFiles.back().m_Document);
		m_vectorFiles.pop_back();
	}
}

const TCHAR* xmlFileContainer::getDir() const
{
	const TCHAR* pReturnVal = getDesc("Dir");
	return pReturnVal;
}

const TCHAR* xmlFileContainer::getTag() const
{
	return getDesc("Tag");
}

void xmlFileContainer::getSchema(XMLDocument *pDocument)
{
	openFile(pDocument, m_szSchemaName.c_str());
}

const TCHAR* xmlFileContainer::getDesc(const TCHAR* szTag) const
{
	XMLElement *pElement = m_Description->FirstChildElement(szTag);
	if (pElement != NULL)
	{
		return pElement->GetText();
	}
	return NULL;
}

const TCHAR* xmlFileContainer::getTag(int iIndex, bool bSkipPrefix)
{
	FAssert(iIndex >= 0);
	FAssert(iIndex < getNumTags());

	return m_szTags[iIndex].c_str() + (bSkipPrefix ? getPrefixLength() : 0);
}

void xmlFileContainer::setTags(XMLElement *pElement)
{
	const TCHAR* szSubType = getDesc("SubType");
	while (pElement != NULL)
	{
		XMLElement *pType = pElement->FirstChildElement("Type");
		if (pType != NULL)
		{
			const char *szName = pType->GetText();
			m_TagCache[szName] = m_szTags.size();
			m_szTags.push_back(szName);
			m_ElementCache[szName] = pElement;
		}
		// check if there is a subtype present
		if (szSubType != NULL)
		{
			XMLElement *pSubType = pElement->FirstChildElement(szSubType);
			if (pSubType != NULL)
			{
				// call the subtype recursively
				// this allows looping through all the subtypes
				// it also means subtypes are check for containing subtypes, but that should never be the case
				setTags(pSubType->FirstChildElement());
			}
		}
		pElement = pElement->NextSiblingElement(getTag());
	}
}

void xmlFileContainer::setAllTags()
{
	m_szTags.clear();
	m_TagCache.clear();
	m_ElementCache.clear();
	for (unsigned int i = 0; i < m_vectorFiles.size(); ++i)
	{
		XMLElement* pElement = m_vectorFiles[i].getDocument()->FirstChildElement(getRoot());
		while (pElement != NULL && strcmp(pElement->Value(), getTag()) != 0)
		{
			pElement = pElement->FirstChildElement();
		}
		setTags(pElement);
	}
	postSetTags();
}

void xmlFileContainer::postSetTags()
{
	if (m_szTags.size() > 1)
	{
		// sort the elements to make them appear alphabetically
		std::sort(m_szTags.begin(), m_szTags.end());

		// get the prefix
		// the idea is that the prefix is the longest possible string where
		// the first and the last tag are identical

		m_szPrefix = m_szTags[0];
		const TCHAR* a = m_szTags[m_szTags.size() - 1].c_str();
		const TCHAR* b = m_szPrefix.c_str();

		// get the full length of the shortest string
		unsigned int iLength = std::min(strlen(a), strlen(b));

		// shorten the comparison by one character as long as the strings aren't identical
		while (iLength > 0 && strncmp(a, b, iLength) != 0)
		{
			--iLength;
		}

		// shorten the prefix to the length just calculated
		if (iLength >= 0)
		{
			m_szPrefix.resize(iLength);
		}
	}
	else
	{
		// list has less than two elements. Too short to determine prefix
		m_szPrefix.clear();
	}
}

void xmlFileContainer::renameType(const TCHAR* szFrom, const TCHAR* szTo)
{
	for (unsigned int i = 0; i < m_vectorFiles.size(); ++i)
	{
		XMLElement* pElement = m_vectorFiles[i].getDocument()->FirstChildElement(getRoot());
		while (pElement != NULL && strcmp(pElement->Value(), getTag()) != 0)
		{
			pElement = pElement->FirstChildElement();
		}
		pElement = pElement->Parent()->ToElement();
		bool bAltered = renameTypeRecursive(pElement, szFrom, szTo);
		
		// write the file if anything changed
		if (bAltered)
		{
			writeFile(i);
		}
	}
}

bool xmlFileContainer::renameTypeRecursive(XMLElement* pElement, const TCHAR* szFrom, const TCHAR* szTo)
{
	XMLElement* pLoopElement = pElement->FirstChildElement();
	if (pLoopElement == NULL)
	{
		const TCHAR* szCurrent = pElement->GetText();
		if (szCurrent != NULL && strcmp(szCurrent, szFrom) == 0)
		{
			// the element has the value from before the type was changed. Overwrite with the new value
			pElement->SetText(szTo);
			// use the return value to tell the file was altered
			return true;
		}
	}

	bool bAltered = false;

	while (pLoopElement != NULL)
	{
		// set bAltered to the return value
		// if the return value is false, set the value to bAltered
		// this way bAltered will be true if at least one loop child returns true
		bAltered = renameTypeRecursive(pLoopElement, szFrom, szTo) || bAltered;
		pLoopElement = pLoopElement->NextSiblingElement();
	}

	return bAltered;
}

bool xmlFileContainer::openFile(XMLDocument *pFile, const TCHAR* szFileName)
{
	CvString szPath;

	bool bIsInMod = true;

	for (int i = 0; CONST_VANILLA_PATH[i] != NULL; ++i)
	{
		szPath = CONST_VANILLA_PATH[i];
		szPath.append(szFileName);
		XMLError eError = pFile->LoadFile(szPath.c_str());

		if (eError == XML_SUCCESS)
		{
			return bIsInMod;
		}
		bIsInMod = false;
	}
	FAssertMsg(false, CvString::format("Failed to locate file %s", szFileName));
	return false;
}

XMLElement* xmlFileContainer::getElement(const TCHAR *szTag) const
{
	if (szTag == NULL)
	{
		// NULL tag name should be treated as any other tag not present in the file 
		return NULL;
	}

	ElementCacheType::const_iterator it = m_ElementCache.find(szTag);
	if (it!=m_ElementCache.end())
	{
		return it->second;
	}
	return NULL;
}

XMLElement* xmlFileContainer::getList(int iIndex) const
{
	XMLElement* pElement = m_vectorFiles[iIndex].getDocument()->FirstChildElement(getRoot());
	while (pElement != NULL && strcmp(pElement->Value(), getTag()) != 0)
	{
		pElement = pElement->FirstChildElement();
	}
	return pElement->Parent()->ToElement();
}

bool xmlFileContainer::isCombo() const
{
	return m_Description->FirstChildElement("SubFiles") != NULL;
}

void xmlFileContainer::writeFile(int iIndex)
{
	FAssert(iIndex >= 0 && iIndex < (int)m_vectorFiles.size());
	CvString szSavePath = CONST_VANILLA_PATH[0];
	szSavePath.append(m_vectorFiles[iIndex].getName());
	XMLError eResult = m_vectorFiles[iIndex].getDocument()->SaveFile(szSavePath.c_str());
	if (eResult != XML_SUCCESS)
	{
		switch (eResult)
		{
			case XML_ERROR_FILE_NOT_FOUND:
				FAssertMsg(false, CvString::format("XML file not found: %s", szSavePath));
				break;
			case XML_ERROR_FILE_COULD_NOT_BE_OPENED:
				FAssertMsg(false, CvString::format("XML file can't be opened: %s\n(opened by other applications?)", szSavePath));
				break;
			default:
				FAssertMsg(false, CvString::format("XML file read error %d: %s", eResult, szSavePath));
		}
	}
}

void xmlFileContainer::writeAllFiles()
{
	int iMax = m_vectorFiles.size();
	// write all files
	// skip first file if m_bIsInMod is not set as this means it's a vanilla file
	for (int i = 0; i < iMax; ++i)
	{
		if (m_bIsInMod || i > 0)
		{
			writeFile(i);
		}
	}
}

GlobalTypeContainer::GlobalTypeContainer(XMLElement *Description)
{
	m_Description = Description->FirstChildElement();

	XMLElement *pElement = m_Description;

	if (strcmp(getTag(), "FootstepAudioTag") == 0)
	{
		// no need to set up this one
		return;
	}


	while (pElement != NULL)
	{
		CvString szTag = pElement->GetText();
		m_szTags.push_back(szTag);
		pElement = pElement->NextSiblingElement();
	}

	postSetTags();
}

const TCHAR* GlobalTypeContainer::getDir() const
{
	return "GlobalTypes";
}

const TCHAR* GlobalTypeContainer::getTag() const
{
	return m_Description->Value();
}


// container sort function. Used by sort
bool sortContainers(const xmlFileContainer* a, const xmlFileContainer* b)
{
	return strcmp(a->getTag(), b->getTag()) < 0;
}

///
///
///    class CyXMLEditor
///
///

CyXMLEditor::CyXMLEditor()
	: m_Document(NULL)
	, m_GlobalTypes(NULL)
	, m_modPath(NULL)
	, m_dllPath(NULL)
	, m_ModSettingsDoc(NULL)
	, m_TextFile(NULL)
	, m_Schema(NULL)
	, m_Info(NULL)
	, m_Keyboard(NULL)
	, m_iMaxGameFontID(0)
	, m_iActiveModFile(0)
	, m_szKeyboard(NULL)
	, m_pFileSpecificInfo(NULL)
	, m_DocCommandFile(new XMLDocument)
{
	m_Document = new XMLDocument;
	m_GlobalTypes = new XMLDocument;
	m_Schema   = new XMLDocument;
	m_Info     = new XMLDocument;
	m_Keyboard = new XMLDocument;

	m_pInstance = this;

	// get the paths to both DLL and mod
	setDLLpath();
	setModPath();

	// update path to mod
	CONST_VANILLA_PATH[0] = getModPath();
	szAutoPath = getModPath();
	szAutoPath.append("TEXT/XML_AUTO_");

	// set the keyboard string
	m_szKeyboard = new TCHAR[KL_NAMELENGTH];
	GetKeyboardLayoutName(m_szKeyboard);

	{
		std::string name = getDLLPath();
		name.append("XML/Editor/CommandText.xml");
		XMLError eError = m_DocCommandFile->LoadFile(name.c_str());
		FAssertMsg(eError == XML_SUCCESS, "Assets/XML/Editor/CommandText.xml not found in Editor mod");
	}

	bool bHasEditorFiles = openFile("Editor/EditorFiles.xml");
	if (!bHasEditorFiles)
	{
		try
		{
			createEditorFiles();
		}
		catch (int e)
		{
			FAssertMsg(false, "Creating editor files failed");
			e = 0;
		}
	}
	XMLElement * pRoot = getRoot("Files");
	FAssertMsg(pRoot != NULL, "Failed to read files in xml/Editor/EditorFiles.xml");
	XMLElement * pElement = pRoot != NULL ? pRoot->FirstChildElement("File") : NULL;
	while (pElement != NULL)
	{
		// create file container
		xmlFileContainer *pContainer = new xmlFileContainer(pElement);
		// store dirname
		m_vectorDirs.push_back(pContainer->getDir());
		// store pointer
		m_szFiles.push_back(pContainer);
		// move on to next file
		pElement = pElement->NextSiblingElement("File");
	}

	openFile("GlobalTypes.xml", m_GlobalTypes);
	pRoot = getRoot("Civ4Types", m_GlobalTypes);
	pElement = pRoot->FirstChildElement();
	while (pElement != NULL)
	{
		// create file container
		GlobalTypeContainer *pContainer = new GlobalTypeContainer(pElement);
		// store dirname
		m_vectorDirs.push_back("GlobalTypes");
		// store pointer
		m_szFiles.push_back(pContainer);
		// move on to next file
		pElement = pElement->NextSiblingElement();
	}

	// sort tagnames (filenames)
	sort(m_szFiles.begin(), m_szFiles.end(), sortContainers);

	// cache index
	for (unsigned int i = 0; i < m_szFiles.size(); ++i)
	{
		m_FileTypeCache[m_szFiles[i]->getTag()] = i;
		m_ContainerCache[m_szFiles[i]->getTag()] = m_szFiles[i];
	}

	// remove dublicated dirs
	// first sort alphabetically
	sort( m_vectorDirs.begin(), m_vectorDirs.end() );
	// next delete any string, which is identical to the last one
	m_vectorDirs.erase( unique( m_vectorDirs.begin(), m_vectorDirs.end() ), m_vectorDirs.end() );

	pRoot = getRoot("MaxGameFontID");
	m_iMaxGameFontID = pRoot != NULL ? pRoot->IntText() : 0;
	FAssertMsg(m_iMaxGameFontID != 0, "Failed to read MaxGameFontID from xml/Editor/EditorFiles.xml");

	readActiveFile();

	// read keys
	openKeyboard();
	pRoot = getRoot("keys", m_Keyboard);
	FAssertMsg(pRoot != NULL, "Corrupted keyboard xml");
	if (pRoot != NULL)
	{
		pElement = pRoot != NULL ? pRoot->FirstChildElement("key") : NULL;
		FAssertMsg(pElement != NULL, "Corrupted keyboard xml");
		while (pElement != NULL)
		{
			m_vectorKeys.push_back(pElement);
			pElement = pElement->NextSiblingElement("key");
		}
	}

	// opening the info file (help text, user assigned tag types etc)
	m_szInfoFileName = "Editor/EditorInfos.xml";
	openFile(m_szInfoFileName, m_Info, true);
	if (m_Info->ErrorID() != XML_SUCCESS)
	{
		XMLNode * pRoot = m_Info->NewElement("Tags");
		m_Info->InsertFirstChild(pRoot);
		writeFile(true);
	}
}

CyXMLEditor::~CyXMLEditor()
{
	while (!m_szFiles.empty())
	{
		SAFE_DELETE(m_szFiles.back());
		m_szFiles.pop_back();
	}

	SAFE_DELETE(m_Document);
	SAFE_DELETE(m_GlobalTypes);
	SAFE_DELETE(m_Info);
#ifndef USING_EDITOR_IN_MOD
	SAFE_DELETE(m_modPath);
#endif
	SAFE_DELETE(m_dllPath);
	SAFE_DELETE(m_ModSettingsDoc);
	SAFE_DELETE(m_Schema);
	SAFE_DELETE(m_Keyboard);
	SAFE_DELETE(m_szKeyboard);
	SAFE_DELETE(m_DocCommandFile);
	iconv::reset();

	m_pInstance = NULL;
}

void CyXMLEditor::createEditorFiles() const
{
	tinyxml2::XMLDeclaration* Header = m_Document->NewDeclaration();
	m_Document->InsertFirstChild(Header);

	// add the icons
	tinyxml2::XMLElement* Icons = m_Document->NewElement("Icons");
	m_Document->InsertEndChild(Icons);
	for (int i = 0; i < (int)(sizeof CONST_ICON_PATHS/sizeof CONST_ICON_PATHS[0]) != NULL; ++i)
	{
		tinyxml2::XMLElement* temp = m_Document->NewElement(CONST_ICON_PATHS[i][0]);
		temp->SetText(CONST_ICON_PATHS[i][1]);
		Icons->InsertEndChild(temp);
	}
	// gamefont end
	//TODO set the end automatically to match the xml
	tinyxml2::XMLElement* GameFont = m_Document->NewElement("MaxGameFontID");
	GameFont->SetText("8950");
	m_Document->InsertEndChild(GameFont);

	// set the languages needed
	{
		XMLElement *Languages = m_Document->NewElement("Languages");
		m_Document->InsertEndChild(Languages);

		Languages->InsertEndChild(m_Document->NewComment(" DefaultString is whatever string should be added for non-English strings. "));

		XMLElement *Default = m_Document->NewElement("DefaultString");
		Default->SetText("FIXME");
		Languages->InsertEndChild(Default);

		Languages->InsertEndChild(m_Document->NewComment(" List of languages to add to xml OTHER than English.                       "));
		Languages->InsertEndChild(m_Document->NewComment(" Order matters and English is assumed to always be first.                  "));

		XMLElement *LangList = m_Document->NewElement("List");
		Languages->InsertEndChild(LangList);

		for (int i = 0; LANGUAGES[i] != NULL; ++i)
		{
			XMLElement *lang = m_Document->NewElement(LANGUAGES[i]);
			lang->SetText(LANGUAGES[i]);
			LangList->InsertEndChild(lang);
		}
	}


	// generate a list of all xml files
	std::vector<std::string> files;
	for (unsigned int i = 0; CONST_VANILLA_PATH[i] != NULL; ++i)
	{
		std::vector<std::string> temp_files = getFiles(CONST_VANILLA_PATH[i]);
		files.insert(files.end(), temp_files.begin(), temp_files.end());
		std::sort(files.begin(), files.end());
		files.erase(std::unique(files.begin(), files.end()), files.end());
	}

	// insert the now sorted list into xml
	tinyxml2::XMLElement* FileElement = m_Document->NewElement("Files");
	m_Document->InsertEndChild(FileElement);

	for (unsigned int i = 0; i < files.size(); ++i)
	{
		tinyxml2::XMLElement* temp = generateFileElementForEditorFiles(files[i].c_str());
		if (temp != NULL)
		{
			FileElement->InsertEndChild(temp);
		}
	}

	// generate the file path
	CvString szPath = getModPath();
	szPath.append("Editor");
	// create the editor directory. Let it silently fail if it's already present. No need to check explicitly
	mkdir(szPath.c_str());
	szPath.append("/EditorFiles.xml");
	tinyxml2::XMLError eResult = m_Document->SaveFile(szPath);

	FAssertMsg(eResult == XML_SUCCESS, CvString::format("Error creating new EditorFiles.xml: %s", XMLDocument::ErrorIDToName(eResult)).c_str());
}

std::vector<std::string> CyXMLEditor::getFiles(const TCHAR* path, const TCHAR* prefix) const
{
	std::vector<std::string> files;
    std::string search_path = path;
	search_path.append("*.*");
    WIN32_FIND_DATA fd; 
    HANDLE hFind = ::FindFirstFile(search_path.c_str(), &fd); 
    if(hFind != INVALID_HANDLE_VALUE) { 
        do { 
            if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (fd.cFileName[0] == '.'
					|| strcmp(fd.cFileName, "Text") == 0
					|| strcmp(fd.cFileName, "text") == 0
					|| strcmp(fd.cFileName, "Editor") == 0
					|| strcmp(fd.cFileName, "Audio") == 0
					|| strcmp(fd.cFileName, "audio") == 0
					|| strcmp(fd.cFileName, "Scenarios") == 0
					|| strcmp(fd.cFileName, "scenarios") == 0
					)
				{
					continue;
				}
                std::string newPrefix = prefix;
				newPrefix.append(fd.cFileName);
				newPrefix.append("/");
				std::string newPath = path;
				newPath.append(fd.cFileName);
				newPath.append("/");
				std::vector<std::string> newFiles = getFiles(newPath.c_str(), newPrefix.c_str());
				files.insert(files.end(), newFiles.begin(), newFiles.end());
            }
			else
			{
				if (strcmp(prefix, "") == 0)
				{
					continue;
				}
				std::string temp = prefix;
				temp.append(fd.cFileName);
				files.push_back(temp);
			}
        }while(::FindNextFile(hFind, &fd)); 
        ::FindClose(hFind); 
    } 
	return files;
}

tinyxml2::XMLElement* CyXMLEditor::generateFileElementForEditorFiles(std::string path) const
{
	tinyxml2::XMLDocument doc;
	openFile(path.c_str(), &doc);

	// ignore any xml file without a 3 level structure (excludes files like schema files)
	tinyxml2::XMLElement* first = doc.FirstChildElement();
	if (first == NULL) return NULL;
	tinyxml2::XMLElement* second = first->FirstChildElement();
	if (second == NULL) return NULL;
	tinyxml2::XMLElement* third = second->FirstChildElement();
	if (third == NULL) return NULL;

	// generate all needed strings
	unsigned index = path.find_last_of("/");

	std::string Dir  = path.substr(0, index);
	std::string Name = path.substr(index+1);
	            Name = Name.substr(0, Name.size()-4);
	std::string Tag  = third->Value();
	std::string Root = first->Value();

	// build the element

	tinyxml2::XMLElement* elementFile  = m_Document->NewElement("File");
	tinyxml2::XMLElement* elementDir   = m_Document->NewElement("Dir");
	elementDir->SetText(Dir.c_str());
	elementFile->InsertEndChild(elementDir);
	tinyxml2::XMLElement* elementName  = m_Document->NewElement("Name");
	elementName->SetText(Name.c_str());
	elementFile->InsertEndChild(elementName);
	tinyxml2::XMLElement* elementTag   = m_Document->NewElement("Tag");
	elementTag->SetText(Tag.c_str());
	elementFile->InsertEndChild(elementTag);
	tinyxml2::XMLElement* elementRoot  = m_Document->NewElement("Root");
	elementRoot->SetText(Root.c_str());
	elementFile->InsertEndChild(elementRoot);


	return elementFile;
}

/**
 * Copy an entire directory tree from one place to another and copy one type of files
 * The target directories are created if needed
 * source: the directory to copy from
 * target: the directory to copy to
 * extension: the file extension for files to copy, example "/*.exe"
 */
void copyDirRecursively(const char *source, const char *target, const char* extension)
{
	// start by making the directory. Let it silently fail if it already exist.
	mkdir(target);

	// directory search of source. List everything and skip any result without the dir attribute set.
    std::string search_path = source;
	search_path.append("/*.*");
    WIN32_FIND_DATA fd; 
    HANDLE hFind = ::FindFirstFile(search_path.c_str(), &fd); 
    if(hFind != INVALID_HANDLE_VALUE) { 
        do { 
            if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (fd.cFileName[0] == '.')
				{
					continue;
				}
				// it's a directory. Append the dir name and call recursively.
				std::string newSource = source;
				newSource.append("/");
				newSource.append(fd.cFileName);
				std::string newTarget = target;
				newTarget.append("/");
				newTarget.append(fd.cFileName);
				copyDirRecursively(newSource.c_str(), newTarget.c_str(), extension);
            }
        }while(::FindNextFile(hFind, &fd)); 
        ::FindClose(hFind); 
    }

	// new directory search. This time list only the files with the chosen file extension.
	search_path = source;
	search_path.append(extension); 
    hFind = ::FindFirstFile(search_path.c_str(), &fd);
	if(hFind != INVALID_HANDLE_VALUE) { 
        do { 
            if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				// Found a file. Set up the paths to copy it.
				std::string newSource = source;
				newSource.append("/");
				newSource.append(fd.cFileName);
				std::string newTarget = target;
				newTarget.append("/");
				newTarget.append(fd.cFileName);
				// Copy and let it silently fail if it already exist.
				CopyFile(newSource.c_str(), newTarget.c_str(), false);

            }
        }while(::FindNextFile(hFind, &fd)); 
        ::FindClose(hFind); 
    }
}

void deleteFilesRecursively(const char *path)
{
	std::string search_path = path;
	search_path.append("/*.*");
	WIN32_FIND_DATA fd;
    HANDLE hFind = ::FindFirstFile(search_path.c_str(), &fd);
	if (hFind != INVALID_HANDLE_VALUE)
	{ 
        do
		{
			if (fd.cFileName[0] == '.')
			{
				continue;
			}
			std::string newPath = path;
			newPath.append("/");
			newPath.append(fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// it's a directory. Call it recursively to empty it and then delete.
				deleteFilesRecursively(newPath.c_str());
				RemoveDirectory(newPath.c_str());
            }
			else
			{
				// It's a file. Just delete the file.
				remove(newPath.c_str());
			}
        }while(::FindNextFile(hFind, &fd)); 
        ::FindClose(hFind); 
    }
	// delete the file/directory itself
	RemoveDirectory(path);
	remove(path);
}

void CyXMLEditor::copyDir(const char *dir, const char* extension) const
{
	// generate the path to the editor (where the files should be copied to)
	std::string target = getDLLPath();
	target.append(dir);

	// when copied, any existing files will be skipped.
	// To make sure we get the updated list, delete all files in the editor before starting.
	deleteFilesRecursively(target.c_str());

	// loop all possible sources for file copying
	for (int i = 0;; ++i)
	{
		const TCHAR *path =	CONST_VANILLA_PATH[i];
		if (path == NULL || strcmp(path, "Assets/XML/") == 0)
		{
			// end of list of sources
			return;
		}
		std::string source = path;
		source.append("../");
		source.append(dir);
	
		// start recursive copy of the located directory.
		copyDirRecursively(source.c_str(), target.c_str(), extension);
	}
}

void CyXMLEditor::copyFiles()
{
	// list graphical locations to copy
	copyDir("Art", "/*.dds");
	copyDir("Res", "/*.tga");

	// quit the editor to force reload of graphics.
	quit();
}

bool CyXMLEditor::altKey() const
{
	return gDLL->altKey();
}

bool CyXMLEditor::shiftKey() const
{
	return gDLL->shiftKey();
}

bool CyXMLEditor::ctrlKey() const
{
	return gDLL->ctrlKey();
}

bool CyXMLEditor::scrollLock() const
{
	return gDLL->scrollLock();
}

bool CyXMLEditor::capsLock() const
{
	return gDLL->capsLock();
}

bool CyXMLEditor::numLock() const
{
	return gDLL->numLock();
}

void CyXMLEditor::quit() const
{
	gDLL->SetDone(true);
}

const CyXMLCommandItem* CyXMLEditor::getCommandItem(const char* name) const
{
	const char* pName = NULL;
	const char* pHelp = NULL;
	const char* pText = NULL;

	XMLElement *pRoot = m_DocCommandFile->FirstChildElement(name);
	if (pRoot != NULL)
	{
		XMLElement *pElement = pRoot->FirstChildElement("Name");
		if (pElement != NULL)
		{
			pName = pElement->GetText();
		}
		pElement = pRoot->FirstChildElement("Help");
		if (pElement != NULL)
		{
			pHelp = pElement->GetText();
		}
		pElement = pRoot->FirstChildElement("Text");
		if (pElement != NULL)
		{
			pText = pElement->GetText();
		}
	}

	if (pName == NULL) pName = "";
	if (pHelp == NULL) pHelp = "";

	return new CyXMLCommandItem(pName, pHelp, pText);
}

bool CyXMLEditor::openFile(const TCHAR* szFileName)
{
	return openFile(szFileName, m_Document, true);
}

bool CyXMLEditor::openFile(const TCHAR* szFileName, XMLDocument *pDoc, bool bIgnoreFileNotFound) const
{
	bool bFileInMod = true;
	XMLError eResult = XML_ERROR_FILE_NOT_FOUND; // needs an init value

	for (int i = 0; CONST_VANILLA_PATH[i] != NULL; ++i)
	{
		CvString szPath = CONST_VANILLA_PATH[i];
		szPath.append(szFileName);
		XMLError eError = pDoc->LoadFile(szPath.c_str());

		if (eError == XML_SUCCESS)
		{
			return bFileInMod;
		}
		bFileInMod = false; // not found in the first try means it's not in vanilla
	}

	FAssertMsg(eResult == XML_SUCCESS || (eResult == XML_ERROR_FILE_NOT_FOUND && bIgnoreFileNotFound), CvString::format("%s read error: %s", szFileName, XMLDocument::ErrorIDToName(eResult)).c_str());
	
	return bFileInMod;
}

void CyXMLEditor::openKeyboard()
{
	CvString szPath = getDLLPath();
	szPath.append("xml/Editor/Keyboard_");
	szPath.append(getKeyboard());
	szPath.append(".xml");

	XMLError eResult = m_Keyboard->LoadFile(szPath.c_str());
	if (eResult == XML_SUCCESS)
	{
		return;
	}
	szPath = getDLLPath();
	szPath.append("xml/Editor/Keyboard.xml");
	eResult = m_Keyboard->LoadFile(szPath.c_str());
	if (eResult == XML_SUCCESS)
	{
		return;
	}
	FAssertMsg(false, "Failed to open keyboard xml file");
}

// write the file currently open
// bInfo makes it save the info file instead
void CyXMLEditor::writeFile(bool bInfo) const
{
	if (!bInfo)
	{
		m_szFiles[m_iActiveFile]->writeFile(m_iActiveModFile);
		return;
	}

	CvString szSavePath = getModPath();
	CvString szFileName;

	XMLDocument *pDoc = NULL;

	if (bInfo)
	{
		szFileName = m_szInfoFileName;
		pDoc = m_Info;
	}
	else
	{
		szFileName = m_szFiles[m_iActiveFile]->getName();
		pDoc = m_Document;
	}

	szSavePath.append(szFileName);

	XMLError eResult = pDoc->SaveFile(szSavePath.c_str());
	if (eResult != XML_SUCCESS)
	{
		switch (eResult)
		{
			case XML_ERROR_FILE_NOT_FOUND:
				FAssertMsg(false, CvString::format("XML file not found: %s", szFileName));
				break;
			case XML_ERROR_FILE_COULD_NOT_BE_OPENED:
				FAssertMsg(false, CvString::format("XML file can't be opened: %s\n(opened by other applications?)", szFileName));
				break;
			default:
				FAssertMsg(false, CvString::format("XML file read error %d: %s", eResult, szFileName));
		}
	}
}

void CyXMLEditor::setTypes(const TCHAR* szOld, const TCHAR* szNew)
{
	// update type cache in the active file
	m_szFiles[m_iActiveFile]->setAllTags();

	// replace szOld with szNew in all files
	if (szOld != NULL && szNew != NULL)
	{
		int iMax = m_szFiles.size();
		for (int i = 0; i < iMax; ++i)
		{
			m_szFiles[i]->renameType(szOld, szNew);
		}
	}
}

int CyXMLEditor::getFileIndex(const TCHAR* szTagName) const
{
	if (szTagName == NULL || strlen(szTagName) == 0)
	{
		return -1;
	}

	FileTypeCacheType::const_iterator it = m_FileTypeCache.find(szTagName);
	if (it!=m_FileTypeCache.end())
	{
		return it->second;
	}
	return -1;
}

XMLElement* CyXMLEditor::getRoot(const TCHAR* szRootName, XMLDocument *pDoc) const
{
	XMLNode *pRoot = pDoc ? pDoc->FirstChild() : m_Document->FirstChild();

	while (pRoot != NULL)
	{
		if (strcmp(pRoot->Value(), szRootName) == 0)
		{
			return pRoot->ToElement();
		}
		pRoot = pRoot->NextSiblingElement(szRootName);
	}
	FAssertMsg(false, CvString::format("Failed to find xml root %s", szRootName));
	return NULL;
}

XMLElement* CyXMLEditor::getSchemaElement(const char *szType)
{
	return m_SchemaCache[szType];
}

void CyXMLEditor::setModPath()
{
#ifdef USING_EDITOR_IN_MOD
	m_modPath = m_dllPath;
#else
	XMLElement *pEditor = getModSettings()->FirstChildElement("Editor");

	const char* pathBuffer = pEditor->FirstChildElement("ModPath")->GetText();
	
	std::string XMLpath = "";
	if (pathBuffer[0] == '.')
	{
		XMLpath = getDLLPath();
	}
	XMLpath.append(pathBuffer);
	XMLpath.append("/XML/");

	m_modPath = new TCHAR[XMLpath.size() + 1];
	memcpy(m_modPath, XMLpath.c_str(), XMLpath.size()*sizeof(TCHAR));
	m_modPath[XMLpath.size()] = 0;

	CONST_VANILLA_PATH[0] = m_modPath;

	// assign BTS paths
	std::vector<const char*> vanillaPaths;
#ifdef COLONIZATION_EXE
	// Colonization also needs to be told where BTS is while BTS use the running exe location
	vanillaPaths.push_back("BTS");
#endif
	vanillaPaths.push_back("Warlords");
	vanillaPaths.push_back("Original");

	unsigned int iVanilla = 4 - vanillaPaths.size();

	for (unsigned int i = 0; i < 3; ++i)
	{
		XMLElement *eTemp = pEditor->FirstChildElement(vanillaPaths[i]);
		if (eTemp != NULL)
		{
			std::string path = eTemp->GetText();
			path.append("/xml/");
			char *str = new char[path.size() + 1];
			memcpy(str, path.c_str(), path.size()*sizeof(CHAR));
			str[path.size()] = 0;
			CONST_VANILLA_PATH[iVanilla] = str;
			++iVanilla;
		}
	}
#endif
}

void CyXMLEditor::setDLLpath()
{
	HMODULE hModule = GetModuleHandle(_T("CvGameCoreDLL.dll"));
	m_dllPath = new TCHAR[MAX_PATH];
	GetModuleFileName(hModule, m_dllPath, MAX_PATH);
	std::string strModPath = m_dllPath;
	unsigned int found = strModPath.find_last_of("\\");
	strModPath = strModPath.substr(0,found + 1);
	memcpy(m_dllPath, strModPath.c_str(), strModPath.size()*sizeof(TCHAR));
	m_dllPath[strModPath.size()] = 0;
}

tinyxml2::XMLDocument* CyXMLEditor::getModSettings()
{
	if (m_ModSettingsDoc != NULL)
	{
		return m_ModSettingsDoc;
	}
	m_ModSettingsDoc = new tinyxml2::XMLDocument;

	std::string szPath = getDLLPath();
	szPath.append("EditorSettings.xml");

	XMLError eResult = m_ModSettingsDoc->LoadFile(szPath.c_str());
	FAssertMsg(eResult == XML_SUCCESS, CvString::format("EditorSettings.xml read error: %s", XMLDocument::ErrorIDToName(eResult)).c_str());
	return m_ModSettingsDoc;
}

int CyXMLEditor::getNumFiles() const
{
	return m_szFiles.size();
}

const TCHAR* CyXMLEditor::getFileName(int iIndex) const
{
	if (iIndex >= 0 && iIndex < getNumFiles())
	{
		return m_szFiles[iIndex]->getName();
	}


	FAssert(iIndex >= 0);
	FAssert(iIndex < getNumFiles());
	return "";
}

int CyXMLEditor::getActiveFile() const
{
	return m_iActiveFile;
}

void CyXMLEditor::setActiveFile(int iIndex)
{
	FAssert(iIndex >= 0);
	FAssert(iIndex < getNumFiles());

	if (iIndex >= 0 && iIndex < getNumFiles())
	{
		CvString szName = m_szFiles[iIndex]->getName();
		m_iActiveFile = iIndex;

		m_szFiles[iIndex]->getSchema(m_Schema);

		// rebuild ElementType hash
		m_SchemaCache.clear();
		XMLElement *pElement = getRoot("Schema", m_Schema);
		pElement = pElement->FirstChildElement("ElementType");
		while (pElement != NULL)
		{
			m_SchemaCache[pElement->Attribute("name")] = pElement;
			pElement = pElement->NextSiblingElement("ElementType");
		}

		// update the file specific info pointer
		m_pFileSpecificInfo = m_Info->FirstChildElement("FileSpecific");
		if (m_pFileSpecificInfo != NULL)
		{
			m_pFileSpecificInfo = m_pFileSpecificInfo->FirstChildElement(m_szFiles[iIndex]->getTag());	
		}

		SAFE_DELETE(m_TextFile);
		CvString szPath = CONST_VANILLA_PATH[0];
		szPath.append("Text/XML_AUTO_");
		szPath.append(m_szFiles[iIndex]->getTag());
		szPath.append(".xml");
		m_TextFile = new TextFileStorage(szPath.c_str(), false);
		// update the cache file to tell which file to start loading on next startup
		writeActiveFile();
	}
}

void CyXMLEditor::readActiveFile()
{
	std::string szPath = getDLLPath();
	szPath.append("EditorCache.xml");

	m_iActiveFile = -1;

	XMLDocument doc;
	XMLError eResult = doc.LoadFile(szPath.c_str());
	if (eResult == XML_SUCCESS)
	{
		XMLElement *element = doc.FirstChildElement("ActiveFile");
		if (element != NULL)
		{
			m_iActiveFile = getFileIndex(element->GetText());
		}
	}

	// check that a valid file was loaded
	if (m_iActiveFile == -1)
	{
		// use units if no valid setup can be loaded (like first run)
		m_iActiveFile = getFileIndex("UnitInfo");
		if (m_iActiveFile == -1)
		{
			// This shouldn't happen, but weird xml files might allow it and it's best to avoid a crash
			m_iActiveFile = 0;
		}
	}
}

void CyXMLEditor::writeActiveFile() const
{
	std::string szPath = getDLLPath();
	szPath.append("EditorCache.xml");

	XMLDocument doc;
	XMLDeclaration* Header = m_Document->NewDeclaration();
	doc.InsertFirstChild(Header);
	XMLElement *element = doc.NewElement("ActiveFile");
	element->SetText(m_szFiles[m_iActiveFile]->getTag());
	doc.InsertEndChild(element);
	doc.SaveFile(szPath.c_str());
}

xmlFileContainer* CyXMLEditor::getFileContainer(const TCHAR* szTag) const
{
	ContainerCacheType::const_iterator it = m_ContainerCache.find(szTag);
	if (it!=m_ContainerCache.end())
	{
		return it->second;
	}
	// File not found
	return NULL;
}

xmlFileContainer* CyXMLEditor::getCurrentFileContainer() const
{
	return m_szFiles[m_iActiveFile];
}

const TCHAR* CyXMLEditor::getInfo(const TCHAR* szTag, const TCHAR* szSetting) const
{
	XMLElement* pTag = NULL;
	if (m_pFileSpecificInfo != NULL)
	{
		// try to use the file specific setting
		pTag = m_pFileSpecificInfo->FirstChildElement(szTag);
	}
	
	if (pTag == NULL)
	{
		// no file specific settings found. Try global settings
		pTag = m_Info->FirstChildElement("Tags");
		if (pTag == NULL)
		{
			FAssertMsg(pTag != NULL, "editor file corrupted");
			return NULL;
		}

		// the the tag in question
		pTag = pTag->FirstChildElement(szTag);
		if (pTag == NULL)
		{
			return NULL;
		}
	}

	// get the child, which contains the requested setting
	pTag = pTag->FirstChildElement(szSetting);
	if (pTag == NULL)
	{
		return NULL;
	}

	return pTag->GetText();
}

const wchar* CyXMLEditor::getInfoWide(const TCHAR* szTag, const TCHAR* szSetting) const
{
	const TCHAR* szReturnVal = getInfo(szTag, szSetting);

	if (szReturnVal != NULL)
	{
		const wchar *szWideReturnVal = iconv::fromUTF8(szReturnVal);
		return szWideReturnVal;
	}
	return L"";
}

void CyXMLEditor::setInfo(bool bFileSpecific, const TCHAR* szTag, const TCHAR* szType, const TCHAR* szHelp, const TCHAR* szClass, bool bAllowTypeNone, bool bRemoteCreate, bool bRemoteCreatePrefix, const TCHAR* szButtonChild)
{
	XMLElement* pRoot = m_Info->FirstChildElement("Tags");
	if (pRoot == NULL)
	{
		FAssertMsg(pRoot != NULL, "editor file corrupted");
		return;
	}

	if (bFileSpecific)
	{
		pRoot = m_pFileSpecificInfo; // use cached pointer to file specific settings
		if (pRoot == NULL)
		{
			// file specific entry doesn't exist. Create it
			XMLElement* pNewRoot = m_Info->FirstChildElement("FileSpecific");
			if (pNewRoot == NULL)
			{
				pNewRoot = m_Info->NewElement("FileSpecific");
				insertElement(m_Info, pNewRoot, sortByValue);
			}
			const TCHAR* szFile = m_szFiles[m_iActiveFile]->getTag();
			pRoot = pNewRoot->FirstChildElement(szFile);
			if (pRoot == NULL)
			{
				pRoot = m_Info->NewElement(szFile);
				insertElement(pNewRoot, pRoot, sortByValue);
				// update cache
				m_pFileSpecificInfo = pRoot;
			}
		}
	}

	XMLElement* pTag = pRoot->FirstChildElement(szTag);
	if (pTag != NULL)
	{
		// remove existing children to start over
		pTag->DeleteChildren();
	}
	else
	{
		// tag is not present in the file.
		pTag = m_Info->NewElement(szTag);
		insertElement(pRoot, pTag, sortByValue);
	}


	// insert all tags while ignoring NULL or empty settings

	if (szType != NULL && strlen(szType) > 0)
	{
		XMLElement* pElement = m_Info->NewElement("Type");
		pElement->SetText(szType);
		pTag->InsertEndChild(pElement);
	}
	if (szHelp != NULL && strlen(szHelp) > 0)
	{
		XMLElement* pElement = m_Info->NewElement("Help");
		pElement->SetText(szHelp);
		pTag->InsertEndChild(pElement);
	}
	if (szClass != NULL && strlen(szClass) > 0)
	{
		XMLElement* pElement = m_Info->NewElement("Class");
		pElement->SetText(szClass);
		pTag->InsertEndChild(pElement);
	}
	if (bAllowTypeNone)
	{
		XMLElement* pElement = m_Info->NewElement("bAllowTypeNone");
		pElement->SetText("1");
		pTag->InsertEndChild(pElement);
	}
	if (bRemoteCreate)
	{
		XMLElement* pElement = m_Info->NewElement("bRemoteCreate");
		pElement->SetText("1");
		pTag->InsertEndChild(pElement);
	}
	if (bRemoteCreatePrefix)
	{
		XMLElement* pElement = m_Info->NewElement("bRemoteCreatePrefix");
		pElement->SetText("1");
		pTag->InsertEndChild(pElement);
	}
	if (szButtonChild != NULL && strlen(szButtonChild) > 0)
	{
		XMLElement* pElement = m_Info->NewElement("ButtonChild");
		pElement->SetText(szButtonChild);
		pTag->InsertEndChild(pElement);
	}
	writeFile(true);
}

int CyXMLEditor::getNumTypes(int iFile) const
{
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->getNumTags();
}

int CyXMLEditor::getComboFile(int iFile, int iIndex) const
{
	FAssert(isCombo(iFile));
	const TCHAR* pType = getType(iFile, iIndex);
	return getFileIndex(pType);
}

const TCHAR* CyXMLEditor::getType(int iFile, int iIndex) const
{
	// index -1 is always NONE
	if (iIndex == -1)
	{
		return "NONE";
	}
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->getTag(iIndex);
}

const TCHAR* CyXMLEditor::getTypeNoPrefix(int iFile, int iIndex) const
{
	// index -1 is always NONE
	if (iIndex == -1)
	{
		return "NONE";
	}
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->getTag(iIndex, true);
}

const TCHAR* CyXMLEditor::getFilePrefix(int iFile) const
{
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->getPrefix();
}

const TCHAR* CyXMLEditor::getFileTag(int iFile) const
{
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->getTag();
}

const TCHAR* CyXMLEditor::getFileDir(int iFile) const
{
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->getDir();
}

const TCHAR* CyXMLEditor::getXMLDir(int iDir) const
{
	FAssert(iDir >= 0);
	FAssert(iDir < getXMLNumDir());
	return m_vectorDirs[iDir];
}

int CyXMLEditor::getXMLNumDir() const
{
	return m_vectorDirs.size();
}

CyXMLObject* CyXMLEditor::getList()
{
	// get schema element of the tag in question
	XMLElement *pElement = m_szFiles[m_iActiveFile]->getList();
	XMLElement *pSchema = m_SchemaCache[pElement->Value()];

	// get schema element of the parent
	CvString szTagParent = m_szFiles[m_iActiveFile]->getRoot();
	XMLElement *pSchemaParent = m_SchemaCache[szTagParent.c_str()];

	// move to the first element from the parent
	// use it to loop through all elements
	pSchemaParent = pSchemaParent->FirstChildElement("element");
	while (pSchemaParent != NULL)
	{
		// stop when an element has the type, which matches the szTag from the start of the function
		const char *Type = pSchemaParent->Attribute("type");
		if (Type != NULL && strcmp(Type, pElement->Value()) == 0)
		{
			break;
		}
		pSchemaParent = pSchemaParent->NextSiblingElement("element");
	}

	CyXMLObject *pObject = new CyXMLObject(m_szFiles[m_iActiveFile]->getList(), NULL, pSchema, pSchemaParent, this);
	return pObject;
}

const TCHAR* CyXMLEditor::getKeyInternal(int iKey, bool bShift, bool bControl, bool bAlt, bool bWide) const
{
	if (iKey < 0 || iKey >= (int)m_vectorKeys.size())
	{
		return NULL;
	}

	XMLElement *pElement = m_vectorKeys[iKey];

	// loop though all possible characters
	pElement = pElement->FirstChildElement("char");
	while (pElement != NULL)
	{
		bool bValid = true;
		const TCHAR* attribute = pElement->Attribute("shift");
		if (!bShift && attribute != NULL)
		{
			bValid = false;
		}
		attribute = pElement->Attribute("control");
		if (!bControl && attribute != NULL)
		{
			bValid = false;
		}
		attribute = pElement->Attribute("alt");
		if (!bAlt && attribute != NULL)
		{
			bValid = false;
		}

		if (!bWide && bValid)
		{
			const TCHAR* pTemp = pElement->GetText();
			while (bValid && pTemp[0] != 0)
			{
				if (pTemp[0] & 0x80)
				{
					bValid = false;
				}
				++pTemp;
			}
		}

		// return the key if modifier keys are valid
		if (bValid)
		{
			return pElement->GetText();
		}
		pElement = pElement->NextSiblingElement("char");
	}

	// no key found
	return NULL;
}

const TCHAR* CyXMLEditor::getKey(int iKey, bool bShift, bool bControl, bool bAlt) const
{
	return getKeyInternal(iKey, bShift, bControl, bAlt, false);
}

std::wstring CyXMLEditor::getWKey(int iKey, bool bShift, bool bControl, bool bAlt) const
{
	const TCHAR* szReturnValue = getKeyInternal(iKey, bShift, bControl, bAlt, true);
	if (szReturnValue == NULL)
	{
		return L"";
	}
	return iconv::fromUTF8(szReturnValue);
}

int CyXMLEditor::getGameFontGroupStart(int iGameFontGroup) const
{
#ifdef USING_EDITOR_IN_MOD
	XMLElement *pElement = m_szFiles[getFileIndex("GameFontInfo")]->getList()->FirstChildElement();

	switch (iGameFontGroup)
	{
		case GAMEFONT_YIELD:       return pElement->FirstChildElement("iYield"   )->IntText();
		case GAMEFONT_BUILDING:    return pElement->FirstChildElement("iBuilding")->IntText();
		case GAMEFONT_BONUS:       return pElement->FirstChildElement("iBonus"   )->IntText();
		case GAMEFONT_FATHERS:     return pElement->FirstChildElement("iFather"  )->IntText();
		case GAMEFONT_MISSIONS:    return pElement->FirstChildElement("iMission" )->IntText();
		case GAMEFONT_FONTSYMBOLS: return gc.getSymbolID(0); // exe hardcoded
		case GAMEFONT_UNITS:       return pElement->FirstChildElement("iUnit"    )->IntText();
		case GAMEFONT_RUSSIAN:     return pElement->FirstChildElement("iRussian" )->IntText();
		
	}
	FAssert(false);
#endif
	return 0;
}

int CyXMLEditor::getGameFontGroupIndex(int iGameFontID) const
{
#ifdef USING_EDITOR_IN_MOD
	int iGroup = -1;
	int iGroupStart = 0;

	for (int i = 1; i < NUM_GAMEFONT_TYPES; ++i)
	{
		int iStart = getGameFontGroupStart(i);
		if (iStart > iGroupStart && iStart <= iGameFontID)
		{
			iGroupStart = iStart;
			iGroup = i;
		}
	}

	FAssertMsg(iGroup != -1, "GameFont group not found");

	int iReturnVal = -10000 * iGroup;
	iReturnVal += iGroupStart - iGameFontID;

	return iReturnVal;
#else
	return 0;
#endif
}

bool CyXMLEditor::isGameFontFile() const
{
	return m_iActiveFile == getFileIndex("GameFontInfo");
}

int CyXMLEditor::getNextGameFontGroupStart(int iHigherThanThis) const
{
#ifdef USING_EDITOR_IN_MOD
	int iReturnVal = MAX_INT;

	for (int i = 1; i < NUM_GAMEFONT_TYPES; ++i)
	{
		int iStart = getGameFontGroupStart(i);
		if (iStart > iHigherThanThis && iStart < iReturnVal)
		{
			iReturnVal = iStart;
		}
	}
	return iReturnVal;
#else
	return 0;
#endif
}

NiColorA CyXMLEditor::getColor(const char *ColorType) const
{
	NiColorA color;
	xmlFileContainer *container = getFileContainer("ColorVal");
	XMLElement *pColor = container != NULL ? container->getElement(ColorType) : NULL;

	if (pColor != NULL)
	{
		color.r = getFloatChild(pColor, "fRed"  );
		color.g = getFloatChild(pColor, "fGreen");
		color.b = getFloatChild(pColor, "fBlue" );
		color.a = getFloatChild(pColor, "fAlpha");
	}

	return color;
}

NiColorA CyXMLEditor::getPlayerColor(const char *PlayerColorType, int iIndex) const
{
	xmlFileContainer *container = getFileContainer("PlayerColorInfo");
	XMLElement *pColor = container->getElement(PlayerColorType);

	switch (iIndex)
	{
	case 0:  return getColor(getTextChild(pColor, "ColorTypePrimary"  ));
	case 1:  return getColor(getTextChild(pColor, "ColorTypeSecondary"));
	case 2:  return getColor(getTextChild(pColor, "TextColorType"     ));
	default: return NiColorA(0,0,0,0);
	}
}

void CyXMLEditor::writeAllFiles()
{
	unsigned int iMax = m_szFiles.size();
	for (unsigned int i = 0; i < iMax; ++i)
	{
		m_szFiles[i]->writeAllFiles();
	}
}

const TCHAR* CyXMLEditor::getIcon(const TCHAR* szIcon) const
{
	XMLElement *pRoot = getRoot("Icons");
	if (pRoot != NULL)
	{
		XMLElement *pChild = pRoot->FirstChildElement(szIcon);
		if (pChild != NULL)
		{
			const TCHAR *pReturnVal = pChild->GetText();
			return pReturnVal;
		}
	}
	return NULL;
}

const TCHAR* CyXMLEditor::getButtonArt(const XMLElement* pElement) const
{
	if (pElement == NULL)
	{
		// no element means no set icon
		return NULL;
	}

	const TCHAR* pType = getInfo(pElement->Name(), "Type");
	if (pType != NULL)
	{
		// The element is a string of type Type
		// Look up the string in the file set in settings.
		ContainerCacheType::const_iterator it = m_ContainerCache.find(pType);
		if (it!=m_ContainerCache.end())
		{
			return getButtonArt(it->second->getElement(pElement->GetText()));
		}
		// File not found
		return NULL;
	}

	// get the name of the child tag, which is used to set icon
	const TCHAR* pChild = getInfo(pElement->Name(), "ButtonChild");
	if (pChild == NULL)
	{
		// nothing set. Use the default
		pChild = "Button";
	}

	const XMLElement* pChildElement = pElement->FirstChildElement(pChild);
	if (pChildElement != NULL)
	{
		// child found (telling that pElement is a dir). Call recursively on the child
		return getButtonArt(pChildElement);
	}

	// check if pElement is of type Button
	const TCHAR* pClass = getInfo(pElement->Name(), "Class");
	if (pClass != NULL && strcmp(pClass, "Button") == 0)
	{
		// it is, which means it should return the contents
		return pElement->GetText();
	}

	// the code ended with something (possibly invalid), which failed to provide button art
	return NULL;
}

bool CyXMLEditor::isColonizationExe() const
{
#ifdef COLONIZATION_EXE
	return true;
#else
	return false;
#endif
}

bool CyXMLEditor::isEditorInMod() const
{
#ifdef USING_EDITOR_IN_MOD
	return true;
#else
	return false;
#endif
}

const TCHAR* CyXMLEditor::getKeyboard() const
{
	return m_szKeyboard;
}

void CyXMLEditor::resetKeyboard()
{
	XMLElement *pElement = m_Keyboard->FirstChildElement("keys");
	pElement = pElement->FirstChildElement("key");

	while (pElement != NULL)
	{
		while (pElement->FirstChildElement("char") != NULL)
		{
			pElement->DeleteChild(pElement->FirstChildElement("char"));
		}

		pElement = pElement->NextSiblingElement("key");
	}
}

void CyXMLEditor::setKeyboardKey(std::wstring szKey, int iIndex, bool bShift, bool bControl, bool bAlt)
{
	XMLElement *pElement = m_Keyboard->FirstChildElement("keys");
	pElement = pElement->FirstChildElement("key");

	for (int i = 0; pElement != NULL && i < iIndex; ++i)
	{
		pElement = pElement->NextSiblingElement("key");
	}

	if (pElement != NULL)
	{
		int iNewID = 0;
		iNewID += bShift   ? 4 : 0;
		iNewID += bControl ? 2 : 0;
		iNewID += bAlt     ? 1 : 0;
		

		XMLElement *pKey = pElement->FirstChildElement("char");

		XMLElement *pPrev = pElement->FirstChildElement("enum");

		bool bOverwrite = false;

		while (pKey != NULL)
		{
			int iKeyID = 0;
			iKeyID += pKey->Attribute("shift")   != NULL ? 4 : 0;
			iKeyID += pKey->Attribute("control") != NULL ? 2 : 0;
			iKeyID += pKey->Attribute("alt")     != NULL ? 1 : 0;
			if (iNewID < iKeyID)
			{
				pPrev = pKey;
			}
			else if (iNewID == iKeyID)
			{
				// key combo exist. Overwrite the existing text
				bOverwrite = true;
				pKey->SetText(iconv::toUTF8(szKey.c_str()));
				break;
			}

			pKey = pKey->NextSiblingElement("char");
		}
		if (!bOverwrite)
		{
			// a new key combo. Add a new element
			XMLElement *pNew = m_Keyboard->NewElement("char");
			pNew->SetText(iconv::toUTF8(szKey.c_str()));
			if (bShift)
			{
				pNew->SetAttribute("shift", "1");
			}
			if (bControl)
			{
				pNew->SetAttribute("control", "1");
			}
			if (bAlt)
			{
				pNew->SetAttribute("alt", "1");
			}
			pElement->InsertAfterChild(pPrev, pNew);
		}
		CvString szSavePath = getDLLPath();
		szSavePath.append("xml/Editor/Keyboard_");
		szSavePath.append(getKeyboard());
		szSavePath.append(".xml");
		m_Keyboard->SaveFile(szSavePath.c_str());
	}
}

std::wstring CyXMLEditor::toUnicode(int iKey) const
{
	char buffer[2];
	buffer[0] = iKey;
	buffer[1] = 0;
	const wchar* szReturnVal = iconv::fromCP1252(buffer);
	return szReturnVal;
}

void CyXMLEditor::generateTextFiles()
{
	std::vector<TextFileStorage*> TextFiles;

	typedef stdext::hash_map<std::string /* type name */, TextFileStorage* /* file containing this type */> ElementCacheType;
	ElementCacheType TagCache;

	int iPath = 0;
	while (CONST_VANILLA_PATH[iPath] != NULL)
	{
		++iPath;
	}
	// iPath now contains the number of paths in the game
	//char szLog[256];
	CvString szFuldPath;
	while (iPath > 0)
	{
		--iPath;
		HANDLE hFind;
		WIN32_FIND_DATA data;
		CvString szPath = CONST_VANILLA_PATH[iPath];
		szPath.append("TEXT/");
		CvString szSearchPath = szPath;
		szSearchPath.append("*.xml");

		hFind = FindFirstFile(szSearchPath.c_str(), &data);
		if (hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				szFuldPath = szPath;
				szFuldPath.append(data.cFileName);
				TextFileStorage *pStorage = new TextFileStorage(szFuldPath.c_str(), iPath);
				TextFiles.push_back(pStorage);
				for (int i = 0; i < pStorage->getNumTags(); ++i)
				{
					TagCache[pStorage->getTag(i)] = pStorage;
				}
			} while (FindNextFile(hFind, &data));
			FindClose(hFind);
		}
	}

	// for some reason it requires to be a pointer to a doc, not a doc itself or the game crashes
	XMLDocument *Doc = new XMLDocument;

	for (int i = 0; i < getNumFiles(); ++i)
	{
		xmlFileContainer *pContainer = m_szFiles[i];
		if (pContainer->isCombo() || strcmp(pContainer->getDir(), "GlobalTypes") == 0)
		{
			continue;
		} 

		CvString szPath = szAutoPath;
		szPath.append(pContainer->getTag());
		szPath.append(".xml");
		Doc->Clear();
		Doc->InsertFirstChild(Doc->NewDeclaration("xml version=\"1.0\" encoding=\"ISO-8859-1\""));

		XMLElement *pRoot = Doc->NewElement("Civ4GameText");
		pRoot->SetAttribute("xmlns", "http://www.firaxis.com");
		Doc->InsertEndChild(pRoot);

		bool bChanged = false;
		bool bSourceChanged = false;

		CvString szNewTextTag;
		XMLElement *pLoopElement = pContainer->getList()->FirstChildElement(pContainer->getTag());
		for (;pLoopElement != NULL; pLoopElement = pLoopElement->NextSiblingElement(pContainer->getTag()))
		{
			const TCHAR* szTag = NULL;
			for (int i = 0; i < iNumTXTTags; ++i)
			{
				
				XMLElement *pChild = pLoopElement->FirstChildElement(TXT_TAGS[i]);

				if (pChild != NULL)
				{
					const TCHAR *pDataString = pChild->GetText();
					if (szTag == NULL)
					{
						XMLElement *pType = pLoopElement->FirstChildElement("Type");
						if (pType != NULL)
						{
							szTag = pType->GetText();
						}
					}
					if (szTag == NULL)
					{
						break;
					}
					
					bChanged = true;
					if (pContainer->isInMod())
					{
						szNewTextTag = "TXT_KEY_";
						szNewTextTag.append(szTag);
						szNewTextTag.append(TXT_TAGS_POSTFIX[i]);
					}
					else
					{
						szNewTextTag = pDataString;
					}

					ElementCacheType::const_iterator it = TagCache.end();
					
					if (pDataString != NULL)
					{
						it = TagCache.find(pDataString);
					}

					bool bClone = false;
					
					if (it != TagCache.end())
					{
						TextFileStorage *pOrigStorage = it->second;
						XMLElement *pOriginal = pOrigStorage->getElement(pDataString);
						if (pOriginal != NULL)
						{
							bClone = true;

							XMLElement *pNewRoot = Doc->NewElement("TEXT");
							clone(pOriginal, pNewRoot);
							pOrigStorage->deleteElement(pOriginal);
							XMLElement *pNewTag = pNewRoot->FirstChildElement("Tag");
							if (pNewTag != NULL)
							{
								pNewTag->SetText(szNewTextTag.c_str());
							}

							// insert alphabetically according to Tag
							insertElement(pRoot, pNewRoot, sortByChildTag);

							if (strcmp(szNewTextTag.c_str(), pDataString) != 0)
							{
								pChild->SetText(szNewTextTag.c_str());
								bSourceChanged = true;
							}
						}
					}
					if (!bClone)
					{
						XMLElement *pNewRoot = Doc->NewElement("TEXT");
						XMLElement *pNewChild = Doc->NewElement("Tag");
						pNewChild->SetText(szNewTextTag.c_str());
						pNewRoot->InsertEndChild(pNewChild);
						pNewChild = Doc->NewElement("English");
						pNewChild->SetText(pDataString != NULL && strcmp(pDataString, szNewTextTag.c_str()) != 0 ? pDataString : "????");
						pNewRoot->InsertEndChild(pNewChild);

						// insert other languages
						addLanguages(pNewRoot);

						// insert alphabetically according to Tag
						insertElement(pRoot, pNewRoot, sortByChildTag);
						
						// generate the new string in the data file
						pChild->SetText(szNewTextTag.c_str());
						bSourceChanged = true;
					}
					
				}
				
			}
		}

		if (bChanged)
		{
			saveFile(Doc, szPath.c_str());
		}
		if (bSourceChanged)
		{
			pContainer->writeAllFiles();
		}
		
	}

	// clean up memory
	while (!TextFiles.empty())
	{
		SAFE_DELETE(TextFiles.back());
		TextFiles.pop_back();
	}
	SAFE_DELETE(Doc);
}

void CyXMLEditor::cleanActiveFile()
{
	cleanActiveFileRecursive(m_szFiles[m_iActiveFile]->getList());
	writeFile();
}

// function to get an XMLElement attribute
// will default to "", meaning it avoids the (crashing) issue of NULL pointers 
const char* getAttribute(const XMLElement *pElement, const char *szAttribute)
{
	if (pElement && szAttribute)
	{
		const char *szReturnVal = pElement->Attribute(szAttribute);
		if (szReturnVal)
		{
			return szReturnVal;
		}
	}
	return "";
}

void CyXMLEditor::cleanActiveFileRecursive(XMLElement *pParent)
{
	if (pParent == NULL)
	{
		// this should never happen, but in case it does, make sure it will not crash
		FAssert(false);
		return;
	}

	XMLElement *pElement = pParent->FirstChildElement();
	if (pElement == NULL)
	{
		// xml file empty, hence nothing to reorder or delete
		return;
	}

	XMLElement *pSchema = m_SchemaCache[pParent->Value()];
	XMLElement *pSchemaChild = pSchema->FirstChildElement("element");

	// first ensure that at least one tag is present in both xml file and schema
	if (pSchemaChild == NULL)
	{
		// no tags in schema, delete everything in xml file
		pParent->DeleteChildren();
		return;
	}

	// check if the first tag is correct
	if (strcmp(getAttribute(pSchemaChild, "type"), pElement->Name()) == 0)
	{
		// first tag is correct
		bool bList = strcmp(getAttribute(pSchemaChild, "maxOccurs"), "*") == 0;
		// progress to next in schema
		pSchemaChild = pSchemaChild->NextSiblingElement("element");

		if (bList)
		{
			FAssertMsg(pSchemaChild == NULL, "Having non-list elements after a list is not supported");
			if (pSchemaChild == NULL)
			{
				// remove any non-list elements
				const char *pTag = pElement->Name();
				while (pElement != NULL)
				{
					if (strcmp(pElement->Name(), pTag))
					{
						// delete the element as it's not part of the list
						XMLElement *pBroken = pElement;
						pElement = pElement->PreviousSiblingElement();
						pBroken->GetDocument()->DeleteNode(pBroken);
					}
					else
					{
						cleanActiveFileRecursive(pElement);
						pElement = pElement->NextSiblingElement();
					}
				}
			}
			return;
		}
	}
	else
	{
		// first element is incorrect, or possibly MinOccur = 0
		pElement = NULL;
	}

	// sort the tags to match the schema order
	for (;pSchemaChild != NULL; pSchemaChild = pSchemaChild->NextSiblingElement("element"))
	{
		XMLElement *pLoopElement = pParent->FirstChildElement(getAttribute(pSchemaChild, "type"));
		if (pLoopElement == NULL)
		{
			// tag is not present
			continue;
		}
		
		if (pElement == NULL)
		{
			// this has to be the first tag
			if (pLoopElement != pParent->FirstChildElement())
			{
				// it's not the first tag. Move it to the front
				pParent->InsertFirstChild(pLoopElement);
			}
		}
		else
		{
			// check if the location is ok. It should be right after pElement
			if (pElement->NextSiblingElement() != pLoopElement)
			{
				// it's not. Move it
				pParent->InsertAfterChild(pElement, pLoopElement);
			}
		}

		pElement = pLoopElement;
		cleanActiveFileRecursive(pElement);
	}

	// loop is done. If there are leftover tags, then they aren't used in the schema file and should be deleted
	if (pElement == NULL)
	{
		// the parent have children, but none of them are present in the schema 
		pParent->DeleteChildren();
	}	
	else if (pElement->NextSiblingElement() != NULL)
	{
		pSchemaChild = pSchema->LastChildElement("element");
		bool bList = strcmp(getAttribute(pSchemaChild, "maxOccurs"), "*") == 0;
		
		XMLElement *pLoopElement = pElement->NextSiblingElement();
		while (pLoopElement != NULL)
		{
			if (bList && pLoopElement->Name(), getAttribute(pSchemaChild, "type") == 0)
			{
				// item is a list element, which is ok. Move the pointer forward
				pElement = pLoopElement;

				// call recursively since the list element might have children
				cleanActiveFileRecursive(pElement);
			}
			else
			{
				// the tag should not be here
				pParent->DeleteChild(pLoopElement);
			}
			// prepare for next iteration
			pLoopElement = pElement->NextSiblingElement();
		}
	}
}

bool CyXMLEditor::isCombo(int iFile) const
{
	FAssert(iFile >= 0);
	FAssert(iFile < getNumFiles());
	return m_szFiles[iFile]->isCombo();
}

int CyXMLEditor::getComboFileIndex(int iFile, const TCHAR* szString) const
{
	// get file info
	xmlFileContainer *pContainer = m_szFiles[iFile];

	// loop subfiles
	for (int i = 0; i < pContainer->getNumTags(); ++i)
	{
		const TCHAR* szSubName = pContainer->getTag(i);
		xmlFileContainer *pSubContainer = getFileContainer(szSubName);
		// get pointer to tag with the type szString
		// returns NULL if not found
		if (pSubContainer->getElement(szString) != NULL)
		{
			// the tag has been found. Return the index to this file
			return i;
		}

	}
	// string not found in any of the files
	return -1;
}

XMLElement* CyXMLEditor::getText(const TCHAR* szTXT_KEY) const
{
	if (m_TextFile != NULL && szTXT_KEY != NULL)
	{
		return m_TextFile->getElement(szTXT_KEY);
	}
	return NULL;
}

TextFileStorage* CyXMLEditor::getTextXMLFile() const
{
	return m_TextFile;
}

// add the non-English languages to a TEXT element in a text xml file
void CyXMLEditor::addLanguages(XMLElement *TEXT)
{
	XMLDocument EditorFilesDoc;
	this->openFile("Editor/EditorFiles.xml", &EditorFilesDoc);
	XMLElement *languages = NULL;
	const char *defaultStr = "";
	languages = getRoot("Languages", &EditorFilesDoc);
	if (languages != NULL)
	{
		XMLElement *strElement = languages->FirstChildElement("DefaultString");
		if (strElement != NULL)
		{
			defaultStr = strElement->GetText();
		}
		languages = languages->FirstChildElement("List");
	}

	if (languages != NULL)
	{
		for (XMLElement *element = languages->FirstChildElement(); element != NULL; element = element->NextSiblingElement())
		{
			XMLElement *pNext = TEXT->GetDocument()->NewElement(element->Value());
			pNext->SetText(defaultStr);
			TEXT->InsertEndChild(pNext);
		}
	}
}

//
// private
//

XMLElement* CyXMLEditor::gotoList(int iFile, XMLDocument *pDoc)
{
	if (pDoc == NULL)
	{
		pDoc = m_Document;
	}
	FAssert(pDoc != NULL);

	XMLElement* pRoot = getRoot(m_szFiles[iFile]->getRoot(), pDoc);
	FAssert(pRoot != NULL);
	CvString szString = m_szFiles[iFile]->getTag();
	szString.append("s");
	XMLElement* pElement = pRoot->FirstChildElement(szString);
	pElement = pElement->FirstChildElement(m_szFiles[iFile]->getTag());

	return pElement;
}


CyXMLObject::CyXMLObject()
	: m_pXML(NULL)
	, m_pSchema(NULL)
	, m_pSchemaParent(NULL)
	, m_pXMLparent(NULL)
{
}


CyXMLObject::CyXMLObject(XMLElement *pXML, CyXMLObject *pXMLparent, XMLElement *pSchema, XMLElement *pSchemaParent, CyXMLEditor *pEditor)
	: m_pXML(pXML)
	, m_pSchema(pSchema)
	, m_pSchemaParent(pSchemaParent)
	, m_pXMLparent(pXMLparent)
	, m_pEditor(pEditor)
{
}

XMLElement* CyXMLObject::FirstChildElement(const TCHAR *szName) const
{
	return m_pXML == NULL ? NULL : m_pXML->FirstChildElement(szName);
}

// next object of the same name
// useful for cycling through a list
CyXMLObject* CyXMLObject::getNextSiblingSameName()
{
	FAssert(m_pXML != NULL);
	if (m_pXML != NULL)
	{
		XMLElement *pNext = m_pXML->NextSiblingElement(m_pXML->Name());
		if (pNext != NULL)
		{
			CyXMLObject *pNew = new CyXMLObject(pNext, m_pXMLparent, m_pSchema, m_pSchemaParent, m_pEditor);
			return pNew;
		}
		else if (isAllocated())
		{
			// always append an empty element to allow adding more
			CyXMLObject *pNew = new CyXMLObject(NULL, m_pXMLparent, m_pSchema, m_pSchemaParent, m_pEditor);
			return pNew;
		}
	}
	return NULL;
}

void CyXMLObject::allocate()
{
	FAssertMsg(!isAllocated(), "Allocating node, which is already allocated (memory leak)");
	m_pXML = m_pXMLparent->getDocument()->NewElement(getName());
	m_pXMLparent->insertChild(m_pXML, m_pSchemaParent);
}


void CyXMLObject::insertChild(XMLElement *pChildXML, XMLElement *pChildSchema)
{
	if (!isAllocated())
	{
		allocate();
	}

	// find the previous element
	// start with the element itself. This will make MaxOccurs=* append to the end
	XMLElement* pPrev = NULL;
	XMLElement* pSchema = pChildSchema;
	while (pPrev == NULL && pSchema != NULL)
	{
		pPrev = m_pXML->LastChildElement(pSchema->Attribute("type"));
		pSchema = pSchema->PreviousSiblingElement("element");
	}

	if (pPrev == NULL)
	{
		// no element found. Insert as first
		m_pXML->InsertFirstChild(pChildXML);
	}
	else
	{
		m_pXML->InsertAfterChild(pPrev, pChildXML);
	}
}



CyXMLObject* CyXMLObject::getFirstSchemaChild()
{
	FAssert(m_pSchema != NULL);
	if (m_pSchema != NULL)
	{
		XMLElement *pNewParent = m_pSchema->FirstChildElement("element");
		if (pNewParent != NULL)
		{
			const char *szType = pNewParent->Attribute("type");
			XMLElement *pNewSchema = m_pEditor->getSchemaElement(szType);
			XMLElement *pNewXML = NULL;
			if (m_pXML != NULL)
			{
				pNewXML = m_pXML->FirstChildElement(szType);
			}

			CyXMLObject *pNew = new CyXMLObject(pNewXML, this, pNewSchema, pNewParent, m_pEditor);
			return pNew;
		}
	}
	return NULL;
}

CyXMLObject* CyXMLObject::next()
{
	FAssert(m_pSchemaParent != NULL);
	if (m_pSchemaParent != NULL)
	{
		if (m_pXML != NULL && isListElement())
		{
			CyXMLObject *pTemp = getNextSiblingSameName();
			if (pTemp != NULL)
			{
				return pTemp;
			}
		}
		XMLElement *pNewParent = m_pSchemaParent->NextSiblingElement("element");
		if (pNewParent != NULL)
		{
			const char *szType = pNewParent->Attribute("type");
			XMLElement *pNewSchema = m_pEditor->getSchemaElement(szType);
			XMLElement *pNewXML = m_pXMLparent->FirstChildElement(szType);

			CyXMLObject *pNew = new CyXMLObject(pNewXML, m_pXMLparent, pNewSchema, pNewParent, m_pEditor);
			return pNew;
		}
	}
	return NULL;
}

// schema info

bool CyXMLObject::isAllocated() const
{
	return m_pXML != NULL;
}

bool CyXMLObject::isBool() const
{
	FAssertMsg(m_pSchema != NULL, "isBool() without schema");
	const char *szContents = m_pSchema->Attribute("dt:type");
	return (szContents != NULL && strcmp(szContents, "boolean") == 0);
}

bool CyXMLObject::isInt() const
{
	FAssertMsg(m_pSchema != NULL, "isBool() without schema");
	const char *szContents = m_pSchema->Attribute("dt:type");
	return (szContents != NULL && strcmp(szContents, "int") == 0);
}

bool CyXMLObject::isListElement() const
{
	FAssertMsg(m_pSchemaParent != NULL, "isListElement() without schema parent");
	const char *szMax = m_pSchemaParent->Attribute("maxOccurs");
	if (szMax == NULL || strcmp(szMax, "1") == 0)
	{
		return false;
	}
	FAssert(strcmp(szMax, "*") == 0);
	return true;
}

bool CyXMLObject::isCombo() const
{
	return m_pEditor->isCombo(getInfoType());
}

bool CyXMLObject::isDir() const
{
	FAssertMsg(m_pSchema != NULL, "isDir() without schema");
	const char *szContents = m_pSchema->Attribute("content");
	if (szContents != NULL && strcmp(szContents, "eltOnly") == 0)
	{
		return true;
	}
	return false;
}

// there is no schema flag for strings
// they are detected by elimination, meaning it's a string if it's nothing else
bool CyXMLObject::isString() const
{
	return !isBool() && !isDir() && !isInt();
}

bool CyXMLObject::isMandatory() const
{
	if (isAllocated())
	{
		return true;
	}
	else if (isOptional())
	{
		return false;
	}

	if (isListElement() && m_pXMLparent != NULL)
	{
		XMLElement *pPrev = m_pXMLparent->FirstChildElement(getName());
		if (pPrev != NULL)
		{
			// not the only element in the list
			return false;
		}
	}

	return m_pXMLparent->isMandatory();
}

bool CyXMLObject::isOptional() const
{
	FAssertMsg(m_pSchemaParent != NULL, "isOptional() without schema parent");
	const char *szMin = m_pSchemaParent->Attribute("minOccurs");
	if (szMin != NULL && strcmp(szMin, "0") == 0)
	{
		return true;
	}
	return false;
}

int CyXMLObject::getTextType() const
{
	const TCHAR *szName = getName();
	for (int i = 0; i < iNumTXTTags; ++i)
	{
		if (strcmp(TXT_TAGS[i], szName) == 0)
		{
			return i;
		}
	}
	return -1;
}

bool CyXMLObject::isText() const
{
	return getTextType() != -1;
}

bool CyXMLObject::canDelete() const
{
	if (!isAllocated())
	{
		return false;
	}
	if (isOptional())
	{
		return true;
	}

	// the last possible way to allow deletion is if it's a list with multiple elements
	if (isListElement())
	{
		if (m_pXML->NextSiblingElement(getName()) != NULL || m_pXML->PreviousSiblingElement(getName()) != NULL)
		{
			return true;
		}
	}

	return false;
}

const TCHAR* CyXMLObject::getName() const
{
	FAssertMsg(m_pSchema != NULL, "getName() without schema");
	if (m_pSchema != NULL)
	{
		const char *szName = m_pSchema->Attribute("name");
		return szName;
	}
	return NULL;
}

int CyXMLObject::getGameFontDisplayID() const
{
	int iChar = 0;
	if (isAllocated())
	{
		iChar = m_pXML->IntText(0);
	}

	if (iChar >= 0)
	{
		return iChar;
	}

	iChar = -iChar;

	int iGroup = iChar / 10000;
	int iIndex = iChar % 10000;

	int iReturnVal = m_pEditor->getGameFontGroupStart(iGroup);
	iReturnVal += iIndex;

	return iReturnVal;
}

const TCHAR* CyXMLObject::getInfoTypeString() const
{
	if (isDir() || isBool() || isInt())
	{
		// tag can't have a type
		return NULL;
	}

	const TCHAR* pString = m_pEditor->getInfo(getName(), "Type");
	return pString;
}

int CyXMLObject::getInfoType() const
{
	if (isDir() || isBool() || isInt())
	{
		// tag can't have a type
		return -2;
	}

	const TCHAR* pString = getInfoTypeString();
	return m_pEditor->getFileIndex(pString);
}

bool CyXMLObject::allowsTypeNone() const
{
	return m_pEditor->getInfo(getName(), "bAllowTypeNone") != NULL;
}

bool CyXMLObject::isRemoteCreate() const
{
	return m_pEditor->getInfo(getName(), "bRemoteCreate") != NULL;
}

bool CyXMLObject::isRemoteCreatePrefix() const
{
	return m_pEditor->getInfo(getName(), "bRemoteCreatePrefix") != NULL;
}

const TCHAR* CyXMLObject::getHelp() const
{
	return m_pEditor->getInfo(getName(), "Help");
}

const TCHAR* CyXMLObject::getInfoClass() const
{
	if (isText())
	{
		return "TxtKey";
	}

	return m_pEditor->getInfo(getName(), "Class");
}

const TCHAR* CyXMLObject::getSchemaChild(int iIndex) const
{
	XMLElement* pElement = m_pSchema->FirstChildElement("element");
	int iCounter = 0;
	while (pElement != NULL && iCounter < iIndex)
	{
		++iCounter;
		pElement = pElement->NextSiblingElement("element");
	}
	if (pElement != NULL)
	{
		return pElement->Attribute("type");
	}
	return NULL;
}


int CyXMLObject::getNumSchemaChildren() const
{
	XMLElement* pElement = m_pSchema->FirstChildElement("element");
	int iCounter = 0;
	while (pElement != NULL)
	{
		++iCounter;
		pElement = pElement->NextSiblingElement("element");
	}
	return iCounter;
}

const TCHAR* CyXMLObject::getParentType() const
{
	return m_pXMLparent->getType();
}


void CyXMLObject::setInfo(bool bFileSpecific, int iNewFileForType, const TCHAR* szHelp, const TCHAR* szClass, bool bAllowTypeNone, bool bRemoteCreate, bool bRemoteCreatePrefix, const TCHAR* szButtonChild)
{
	const char *szType = NULL;
	if (iNewFileForType >= 0)
	{
		szType = m_pEditor->getFileTag(iNewFileForType);
	}
	m_pEditor->setInfo(bFileSpecific, getName(), szType, szHelp, szClass, bAllowTypeNone, bRemoteCreate, bRemoteCreatePrefix, szButtonChild);
}

// XML info

const TCHAR* CyXMLObject::getType() const
{
	if (m_pXML != NULL)
	{
		XMLElement *pType = m_pXML->FirstChildElement("Type");
		if (pType == NULL)
		{
			pType = m_pXML->PreviousSiblingElement("Type");
		}
		if (pType == NULL)
		{
			pType = m_pXML->NextSiblingElement("Type");
		}
		if (pType != NULL)
		{
			return pType->GetText();
		}
	}
	return NULL;
}

const TCHAR* CyXMLObject::getChildType() const
{
	if (m_pXML != NULL)
	{
		XMLElement *pType = m_pXML->FirstChildElement("Type");
		if (pType != NULL)
		{
			return pType->GetText();
		}
	}
	return NULL;
}

const TCHAR* CyXMLObject::getValue() const
{
	if (m_pXML != NULL)
	{
		const char *szValue = m_pXML->GetText();
		return szValue;
	}
	return NULL;
}

bool CyXMLObject::getBoolValue() const
{
	if (!isAllocated())
	{
		return false;
	}
	const TCHAR *szValue = getValue();
	return *szValue == '1' && szValue[1] == 0;
}

const TCHAR* CyXMLObject::getChildString(const TCHAR* szChildName) const
{
	if (!isAllocated())
	{
		return NULL;
	}
	XMLElement *pChild = m_pXML->FirstChildElement(szChildName);
	if (pChild == NULL)
	{
		return NULL;
	}
	return pChild->GetText();
}

const TCHAR* CyXMLObject::getButtonArt() const
{
	return m_pEditor->getButtonArt(m_pXML);
}

const TCHAR* CyXMLObject::getButtonArtChild() const
{
	return m_pEditor->getInfo(getName(), "ButtonChild");
}

int CyXMLObject::getActiveComboFile() const
{
	if (!isAllocated() || !isCombo())
	{
		return -1;
	}
	return m_pEditor->getComboFileIndex(getInfoType(), getValue());
}

std::wstring CyXMLObject::getText() const
{
	FAssert(isText());
	if (!isAllocated())
	{
		return L"";
	}

	const TCHAR* pValue = getValue();
	if (pValue == NULL)
	{
		return L"";
	}

	XMLElement *pElement = m_pEditor->getText(pValue);
	if (pElement != NULL)
	{
		pElement = pElement->FirstChildElement("English");
		if (pElement != NULL)
		{
			XMLElement *pText = pElement->FirstChildElement("Text");
			if (pText != NULL)
			{
				pElement = pText;
			}
			return iconv::fromUTF8(pElement->GetText());
		}
	}

	return L"";
}

const CyXMLTextString* CyXMLObject::getTextString() const
{
	FAssert(isText());
	if (!isAllocated())
	{
		return NULL;
	}

	XMLElement *pElement = m_pEditor->getText(getValue());
	if (pElement != NULL)
	{
		pElement = pElement->FirstChildElement("English");
		if (pElement != NULL)
		{
			return new CyXMLTextString(pElement);
		}
	}

	return NULL;
}

// write to XML

void CyXMLObject::setValue(const TCHAR* szNewValue)
{
	if (!isAllocated())
	{
		allocate();
	}

	FAssertMsg(m_pXML != NULL, "setValue() without XML object");
	if (m_pXML != NULL)
	{
		const TCHAR* szOldValue = getValue();
		
		m_pXML->SetText(szNewValue);
		m_pEditor->writeFile();

		if (strcmp(getName(), "Type") == 0)
		{
			// recalculate the type cache
			m_pEditor->setTypes(szOldValue, getValue());

		}
	}
}

void CyXMLObject::setTxtKey(const TCHAR* szMaleSingular, const TCHAR* szMalePlural, const TCHAR* szFemaleSingular, const TCHAR* szFemalePlural)
{
	// first determine which strings should be saved
	// empty strings should be ignored
	// Empty strings are strings of 0 length, but not NULL

	std::vector<bool> vectorFemale;
	std::vector<bool> vectorPlural;
	std::vector<const TCHAR*> vectorStrings;

	// gather info on which strings are present
	if (*szMaleSingular)
	{
		vectorFemale.push_back(false);
		vectorPlural.push_back(false);
		vectorStrings.push_back(szMaleSingular);
	}
	if (*szFemaleSingular)
	{
		vectorFemale.push_back(true);
		vectorPlural.push_back(false);
		vectorStrings.push_back(szFemaleSingular);
	}
	if (*szMalePlural)
	{
		vectorFemale.push_back(false);
		vectorPlural.push_back(true);
		vectorStrings.push_back(szMalePlural);
	}
	if (*szFemalePlural)
	{
		vectorFemale.push_back(true);
		vectorPlural.push_back(true);
		vectorStrings.push_back(szFemalePlural);
	}

	// ignore calls, which deletes everything
	unsigned int iVectorSize = vectorPlural.size();
	if (iVectorSize == 0)
	{
		return;
	}

	// figure out how the strings are distributed in the two bool categories
	unsigned int iGender = 0;
	unsigned int iPlural = 0;

	for (unsigned int i = 0; i < iVectorSize; ++i)
	{
		iGender |= vectorFemale[i] ? 2 : 1;
		iPlural |= vectorPlural[i] ? 2 : 1;
	}

	CvString szText = vectorStrings[0];

	for (unsigned int i = 1; i < iVectorSize; ++i)
	{
		szText.append(":");
		szText.append(vectorStrings[i]);
	}

	// the following requires the element to be allocated
	if (!isAllocated())
	{
		allocate();
	}

	// assign a TXT_KEY string if none is present
	if (getValue() == NULL || (*getValue()) == 0)
	{
		CvString szTag = "TXT_KEY_";
		szTag.append(getType());
		int iType = getTextType();
		FAssert(iType >= 0 && iType < 4);
		szTag.append(TXT_TAGS_POSTFIX[iType]);
		setValue(szTag.c_str());
	}


	TextFileStorage *pFile = m_pEditor->getTextXMLFile();

	// 
	XMLElement *pElement = pFile->getElement(getValue());
	if (pElement)
	{
		pElement = pElement->FirstChildElement("English");
	}

	if (pElement == NULL)
	{
		pElement = pFile->createElement(getValue());
	}

	// reset text and children
	pElement->DeleteChildren();

	if (iGender == 1 && (iPlural & 1))
	{
		// conditions are met to write in one line only
		pElement->SetText(szText);
	}
	else
	{
		// append needed children
		XMLElement *pText   = pElement->GetDocument()->NewElement("Text");
		XMLElement *pGender = pElement->GetDocument()->NewElement("Gender");
		XMLElement *pPlural = pElement->GetDocument()->NewElement("Plural");

		pElement->InsertEndChild(pText);
		pElement->InsertEndChild(pGender);
		pElement->InsertEndChild(pPlural);
		
		pText->SetText(szText);

		// set gender info
		CvString szBuffer;
		for (unsigned int i = 0; i < iVectorSize; ++i)
		{
			if (i > 0)
			{
				szBuffer.append(":");
			}
			szBuffer.append(vectorFemale[i] ? "Female" : "Male");
			if (iGender != 3)
			{
				break;
			}
		}
		pGender->SetText(szBuffer);

		// set plural info
		szBuffer.clear();
		for (unsigned int i = 0; i < iVectorSize; ++i)
		{
			if (i > 0)
			{
				szBuffer.append(":");
			}
			szBuffer.append(vectorPlural[i] ? "1" : "0");
			if (iPlural != 3)
			{
				break;
			}
		}
		pPlural->SetText(szBuffer);
	}
	
	// write the new text xml file
	pFile->save();
}

void CyXMLObject::createRemote()
{
	// first ignore calls from entries without a type
	const TCHAR* szTypeOfFirstElement = getParentType();
	if (szTypeOfFirstElement == NULL)
	{
		return;
	}

	const TCHAR* pInfoType = m_pEditor->getInfo(getName(), "Type");

	// access the file containers
	xmlFileContainer* pCurrentFileContainer = m_pEditor->getCurrentFileContainer();
	xmlFileContainer* pFileContainer = m_pEditor->getFileContainer(pInfoType);

	// generate the new value
	// prefix + type
	// type has to add the length of prefix unless the prefix is included as well
	CvString szNewValue = pFileContainer->getPrefix();
	szNewValue.append(szTypeOfFirstElement + (isRemoteCreatePrefix() ? 0 : pCurrentFileContainer->getPrefixLength()));

	// check if the remote entry exists
	if (pFileContainer->getElement(szNewValue.c_str()) == NULL)
	{
		// no such remote Type. Create it
		XMLElement* pParent = pFileContainer->getList();
		XMLElement* pClone = pParent->GetDocument()->NewElement(pInfoType);
		pParent->InsertEndChild(pClone);
		cloneRecursive(pParent->FirstChildElement(pInfoType), pClone, szNewValue.c_str());

		// save the changed file
		pFileContainer->writeFile(0);

		// update cache
		pFileContainer->setAllTags();
	}

	// write the newly created Type to this element
	setValue(szNewValue.c_str());
}

void CyXMLObject::setGameFontChar(int iNewValue)
{
	if (!isAllocated())
	{
		if (iNewValue == 0)
		{
			return;
		}
		allocate();
	}

	if (!m_pEditor->isGameFontFile())
	{
		// use the groups unless it's the file, which sets the groups
		iNewValue = m_pEditor->getGameFontGroupIndex(iNewValue);
	}

	m_pXML->SetText(CvString::format("%d", iNewValue).c_str());
	m_pEditor->writeFile();
}

void CyXMLObject::deleteXMLEntry()
{
	if (!isAllocated())
	{
		FAssertMsg(isAllocated(), "Trying to delete non-existing XML data");
		return;
	}

	XMLNode *pParent = m_pXML->Parent();
	pParent->DeleteChild(m_pXML);
	m_pXML = NULL;
	m_pEditor->writeFile();

	if (strcmp(getName(), "Type") == 0)
	{
		// recalculate the type cache
		m_pEditor->setTypes(NULL, NULL);
	}
}

void CyXMLObject::dragTo(CyXMLObject *pDest)
{
	if (strcmp(getName(), pDest->getName()))
	{
		// make sure the elements are of the same type, hence one list
		return;
	}

	XMLNode *pParent = m_pXML->Parent();
	if (!pDest->isAllocated())
	{
		// pDest is the last object in the list
		// this means m_pXML should be moved to the end
		pParent->InsertEndChild(m_pXML);
	}
	else
	{
		// here is a minor problem. This->m_pXML should be inserted before pDest
		// However tinyxml2 only supports inserting after an element
		// the solution is to get the element before pDest and insert after that one
		XMLElement *pPrev = pDest->PreviousElement();
		if (pPrev != NULL)
		{
			pParent->InsertAfterChild(pPrev, m_pXML);
		}
		else
		{
			// turns out pDest is the first element
			// insert in the front of the list
			pParent->InsertFirstChild(m_pXML);
		}
	}
	m_pEditor->writeFile();
}

void CyXMLObject::clone()
{
	// avoid working on elements, which can't be cloned
	// the GUI shouldn't allow this, but just to be safe
	if (!isAllocated() || !isListElement())
	{
		return;
	}

	// create a new "root" element with the same name as this
	XMLNode *pParent = m_pXML->Parent();
	tinyxml2::XMLElement* pClone = getDocument()->NewElement(getName());

	// insert after this
	pParent->InsertAfterChild(m_pXML, pClone);

	// clone children
	cloneRecursive(m_pXML, pClone, NULL);

	// write the result
	m_pEditor->writeFile();
}

void CyXMLObject::cloneRecursive(tinyxml2::XMLElement* pOriginal, tinyxml2::XMLElement* pClone, const TCHAR* szNewType)
{
	// make sure pClone has the same text/children contents as pOriginal

	tinyxml2::XMLElement* pLoop = pOriginal->FirstChildElement();
	if (pLoop == NULL)
	{
		// no children. Try to clone the text
		const TCHAR* szText = pOriginal->GetText();
		if (strcmp(pOriginal->Value(), "Type") == 0)
		{
			szText = szNewType;
		}
		if (szText != NULL)
		{
			pClone->SetText(szText);
		}
	}
	// loop the children
	for(;pLoop != NULL; pLoop = pLoop->NextSiblingElement())
	{
		if (strcmp(pLoop->Value(), "Type") == 0 && szNewType == NULL)
		{
			// do not clone types
			continue;
		}

		// make a new child with the same value (name)
		tinyxml2::XMLElement* pNew = pClone->GetDocument()->NewElement(pLoop->Value());

		// add it to the clone
		pClone->InsertEndChild(pNew);

		// call recursively to clone children/text contents to the new element
		cloneRecursive(pLoop, pNew, szNewType);
	}
}

XMLElement* CyXMLObject::PreviousElement() const
{
	return m_pXML->PreviousSiblingElement(getName());
}

XMLDocument* CyXMLObject::getDocument() const
{
	if (isAllocated())
	{
		return m_pXML->GetDocument();
	}
	return m_pXMLparent->getDocument();
}

CyXMLCommandItem::CyXMLCommandItem()
	: m_szName(NULL)
	, m_szHelp(NULL)
	, m_szText(NULL)
{
}

CyXMLCommandItem::CyXMLCommandItem(const char *szName, const char *szPopupHelp, const char *szFullText)
	: m_szName(szName)
	, m_szHelp(szPopupHelp)
	, m_szText(szFullText)
{
}

const char* CyXMLCommandItem::getName() const
{
	return m_szName;
}
const char* CyXMLCommandItem::getHelp() const
{
	return m_szHelp;
}
const char* CyXMLCommandItem::getText() const
{
	return m_szText;
}