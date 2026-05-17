#include "scrapers/HowLongToBeatScraper.h"

#include "FileData.h"
#include "Log.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <chrono>
#include <memory>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace rapidjson;

namespace
{
	const std::string HLTB_BASE_URL = "https://howlongtobeat.com";

	std::string getCleanGameName(const ScraperSearchParams& params)
	{
		auto cleanName = params.nameOverride;
		if (cleanName.empty())
			cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(params.game->getPath()));

		return Utils::String::trim(cleanName);
	}

	std::string getTimestamp()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}

	HttpReqOptions getBaseOptions()
	{
		HttpReqOptions options;
		options.userAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36";
		options.customHeaders.push_back("Accept: application/json");
		options.customHeaders.push_back("Referer: " + HLTB_BASE_URL + "/");
		return options;
	}

	int getIntMember(const Value& value, const char* key)
	{
		if (!value.IsObject() || !value.HasMember(key))
			return 0;

		const Value& member = value[key];
		if (member.IsInt())
			return member.GetInt();
		if (member.IsString())
			return Utils::String::toInteger(member.GetString());

		return 0;
	}

	std::string getStringMember(const Value& value, const char* key)
	{
		if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString())
			return "";

		return value[key].GetString();
	}
}

void HowLongToBeatScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	auto cleanName = getCleanGameName(params);
	if (cleanName.empty())
		return;

	requests.push(std::unique_ptr<ScraperRequest>(new HowLongToBeatRequest(results, params.game, cleanName)));
}

bool HowLongToBeatScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& HowLongToBeatScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds = {};
	return mdds;
}

HowLongToBeatRequest::HowLongToBeatRequest(std::vector<ScraperSearchResult>& resultsWrite, FileData* game, const std::string& searchName)
	: ScraperRequest(resultsWrite),
	mGame(game),
	mSearchName(searchName),
	mStage(Stage::Init)
{
	setStatus(ASYNC_IN_PROGRESS);
	startInit();
}

void HowLongToBeatRequest::startInit()
{
	auto options = getBaseOptions();
	mRequest.reset(new HttpReq(HLTB_BASE_URL + "/api/bleed/init?t=" + getTimestamp(), &options));
}

void HowLongToBeatRequest::startSearch()
{
	auto options = getBaseOptions();
	options.customHeaders.push_back("Content-Type: application/json");
	options.customHeaders.push_back("Origin: " + HLTB_BASE_URL);
	options.customHeaders.push_back("x-auth-token: " + mToken);
	options.customHeaders.push_back("x-hp-key: " + mHpKey);
	options.customHeaders.push_back("x-hp-val: " + mHpVal);
	options.dataToPost = buildSearchBody();
	mStage = Stage::Search;
	mRequest.reset(new HttpReq(HLTB_BASE_URL + "/api/bleed", &options));
}

void HowLongToBeatRequest::update()
{
	if (!mRequest)
	{
		setStatus(ASYNC_DONE);
		return;
	}

	auto status = mRequest->status();
	if (status == HttpReq::REQ_IN_PROGRESS)
		return;

	if (status != HttpReq::REQ_SUCCESS)
	{
		LOG(LogWarning) << "HowLongToBeatRequest - HTTP request failed: " << status << " " << mRequest->getErrorMsg();
		setStatus(ASYNC_DONE);
		return;
	}

	auto response = mRequest->getContent();
	if (mStage == Stage::Init)
	{
		if (!processInit(response))
		{
			setStatus(ASYNC_DONE);
			return;
		}

		startSearch();
		return;
	}

	processSearch(response);
	setStatus(ASYNC_DONE);
}

bool HowLongToBeatRequest::processInit(const std::string& response)
{
	Document doc;
	doc.Parse(response.c_str());
	if (doc.HasParseError())
	{
		LOG(LogWarning) << "HowLongToBeatRequest - Error parsing init JSON: " << GetParseError_En(doc.GetParseError());
		return false;
	}

	mToken = getStringMember(doc, "token");
	mHpKey = getStringMember(doc, "hpKey");
	mHpVal = getStringMember(doc, "hpVal");

	return !mToken.empty() && !mHpKey.empty() && !mHpVal.empty();
}

std::string HowLongToBeatRequest::buildSearchBody() const
{
	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);

	writer.StartObject();
	writer.Key("searchType"); writer.String("games");
	writer.Key("searchTerms");
	writer.StartArray();
	for (auto term : Utils::String::split(mSearchName, ' ', true))
		writer.String(term.c_str());
	writer.EndArray();
	writer.Key("searchPage"); writer.Int(1);
	writer.Key("size"); writer.Int(8);
	writer.Key("searchOptions");
	writer.StartObject();
	writer.Key("games");
	writer.StartObject();
	writer.Key("userId"); writer.Int(0);
	writer.Key("platform"); writer.String("");
	writer.Key("sortCategory"); writer.String("popular");
	writer.Key("rangeCategory"); writer.String("main");
	writer.Key("rangeTime");
	writer.StartObject();
	writer.Key("min"); writer.Int(0);
	writer.Key("max"); writer.Int(0);
	writer.EndObject();
	writer.Key("gameplay");
	writer.StartObject();
	writer.Key("perspective"); writer.String("");
	writer.Key("flow"); writer.String("");
	writer.Key("genre"); writer.String("");
	writer.Key("difficulty"); writer.String("");
	writer.EndObject();
	writer.Key("rangeYear");
	writer.StartObject();
	writer.Key("min"); writer.String("");
	writer.Key("max"); writer.String("");
	writer.EndObject();
	writer.Key("modifier"); writer.String("");
	writer.EndObject();
	writer.Key("users");
	writer.StartObject();
	writer.Key("sortCategory"); writer.String("postcount");
	writer.EndObject();
	writer.Key("lists");
	writer.StartObject();
	writer.Key("sortCategory"); writer.String("follows");
	writer.EndObject();
	writer.Key("filter"); writer.String("");
	writer.Key("sort"); writer.Int(0);
	writer.Key("randomizer"); writer.Int(0);
	writer.EndObject();
	writer.Key("useCache"); writer.Bool(true);
	writer.Key(mHpKey.c_str()); writer.String(mHpVal.c_str());
	writer.EndObject();

	return buffer.GetString();
}

void HowLongToBeatRequest::processSearch(const std::string& response)
{
	Document doc;
	doc.Parse(response.c_str());
	if (doc.HasParseError())
	{
		LOG(LogWarning) << "HowLongToBeatRequest - Error parsing search JSON: " << GetParseError_En(doc.GetParseError());
		return;
	}

	if (!doc.HasMember("data") || !doc["data"].IsArray())
		return;

	const Value& data = doc["data"];
	for (SizeType i = 0; i < data.Size(); i++)
	{
		const Value& game = data[i];
		auto id = getIntMember(game, "game_id");
		auto name = getStringMember(game, "game_name");
		if (id <= 0 || name.empty())
			continue;

		ScraperSearchResult result("HowLongToBeat");
		if (mGame != nullptr)
			result.mdl = mGame->getMetadata();

		result.mdl.set(MetaDataId::Name, name);
		result.mdl.set(MetaDataId::HltbId, std::to_string(id));
		result.mdl.set(MetaDataId::HltbUrl, HLTB_BASE_URL + "/game/" + std::to_string(id));

		auto mainTime = getIntMember(game, "comp_main");
		auto extraTime = getIntMember(game, "comp_plus");
		auto completionistTime = getIntMember(game, "comp_100");
		auto allStylesTime = getIntMember(game, "comp_all");

		if (mainTime > 0)
			result.mdl.set(MetaDataId::HltbMainTime, std::to_string(mainTime));
		if (extraTime > 0)
			result.mdl.set(MetaDataId::HltbExtraTime, std::to_string(extraTime));
		if (completionistTime > 0)
			result.mdl.set(MetaDataId::HltbCompletionistTime, std::to_string(completionistTime));
		if (allStylesTime > 0)
			result.mdl.set(MetaDataId::HltbAllStylesTime, std::to_string(allStylesTime));

		mResults.push_back(result);
	}
}
