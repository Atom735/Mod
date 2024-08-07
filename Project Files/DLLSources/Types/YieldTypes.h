#pragma once

#include "../autogenerated/AutoXmlEnum.h"

class CvYieldInfo;

class LoopYieldTypes
{
public:
	LoopYieldTypes();
	LoopYieldTypes(YieldTypes type);
	YieldTypes value() const;

	bool isValid() const;
	bool next();

	const CvYieldInfo& info() const;
	operator YieldTypes() const;

	static LoopYieldTypes createFromInt(int iIndex);

	static const CvYieldInfo& info(YieldTypes eYield);
protected:
	YieldTypes data;
};

class CargoYieldTypes : public LoopYieldTypes
{
public:
	CargoYieldTypes();
	CargoYieldTypes(YieldTypes type);

	bool isValid() const;
	bool next();
	static CargoYieldTypes createFromInt(int iIndex);
};

class LuxuryYieldTypes : public LoopYieldTypes
{
public:
	LuxuryYieldTypes();
	LuxuryYieldTypes(YieldTypes type);

	bool isValid() const;
	bool next();
	static LuxuryYieldTypes createFromInt(int iIndex);
};

class PlotYieldTypes : public LoopYieldTypes
{
public:
	PlotYieldTypes();
	PlotYieldTypes(YieldTypes type);

	bool isValid() const;
	bool next();
	static PlotYieldTypes createFromInt(int iIndex);
};

#if 0
class CargoYieldTypes
{
public:
	CargoYieldTypes();
	CargoYieldTypes(YieldTypes type);
	YieldTypes value() const;

	bool isValid() const;
	bool next();

	const CvYieldInfo& info() const;
	operator YieldTypes() const;

	static CargoYieldTypes createFromInt(int iIndex);

	static const CvYieldInfo& info(YieldTypes eYield);
protected:
	YieldTypes data;
};

class LuxuryYieldTypes
{
public:
	LuxuryYieldTypes();
	LuxuryYieldTypes(YieldTypes type);
	YieldTypes value() const;

	bool isValid() const;
	bool next();

	const CvYieldInfo& info() const;
	operator YieldTypes() const;

	static LuxuryYieldTypes createFromInt(int iIndex);

	static const CvYieldInfo& info(YieldTypes eYield);
protected:
	YieldTypes data;
};

class PlotYieldTypes
{
public:
	PlotYieldTypes();
	PlotYieldTypes(YieldTypes type);
	YieldTypes value() const;

	bool isValid() const;
	bool next();

	const CvYieldInfo& info() const;
	operator YieldTypes() const;

	static PlotYieldTypes createFromInt(int iIndex);

	static const CvYieldInfo& info(YieldTypes eYield);
protected:
	YieldTypes data;
};
#endif
