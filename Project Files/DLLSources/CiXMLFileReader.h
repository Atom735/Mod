#pragma once

#include "CiXMLReader.h"
#include "tinyxml2.h"

class CiXMLFileReader;

class CiXMLTypeContainer
{
public:
	CiXMLTypeContainer(const CiXMLFileReader& Reader);

	CiXMLReader getListElement();
	void next();

	const char* getType() const;

	bool valid() const;

private:
	void setType();

	const CiXMLFileReader& m_Reader;
	const tinyxml2::XMLElement* m_pElement;
	CvString m_szType;
};


class CiXMLFileNameHolderBase
{
public:
	virtual const char* getFileName() const = 0;
	virtual const char* getType() const = 0;
};

template<typename T>
class CiXMLFileNameHolder : public CiXMLFileNameHolderBase
{
public:
	const char* getFileName() const;
	const char* getType() const;
};

class CiXMLFileReader
{
	friend class CiXMLTypeContainer;
public:
	template<typename T>
	CiXMLFileReader(T var) 
		: m_pFile(NULL)
		, m_pSchema(NULL)
		, m_pRoot(NULL)
		, m_pSchemaRoot(NULL)
	{ m_FileNameHolder = new CiXMLFileNameHolder<T>(); openFile();  }
	~CiXMLFileReader();

	void validate(CvXMLLoadUtility* pUtility) const;

	int getNumTypes() const;

	// warning: CiXMLTypeContainer and any CiXMLReader it spawns can't be used after clearCache is called
	CiXMLTypeContainer getFirstListElement() const;

	// remove the cached files to free up memory
	static void clearCache();

private:
	void openFile();
	const tinyxml2::XMLElement* getFirstElement() const;

	const tinyxml2::XMLDocument* m_pFile;
	const tinyxml2::XMLDocument* m_pSchema;

	const tinyxml2::XMLElement* m_pRoot;
	const tinyxml2::XMLElement* m_pSchemaRoot;
	const char* m_xmlns;

	CiXMLFileNameHolderBase* m_FileNameHolder;
};

template<typename T>
const char* CiXMLFileNameHolder<T>::getType() const
{
	return "Type";
}
