#include "scrapers/PCGamingWikiScraper.h"

#include "FileData.h"
#include "Log.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <vector>

using namespace rapidjson;

namespace
{
	const std::string PCGW_API_URL = "https://www.pcgamingwiki.com/w/api.php";
	const std::string PCGW_PAGE_URL = "https://www.pcgamingwiki.com/wiki/";

	bool isDigits(const std::string& value)
	{
		return !value.empty() && std::all_of(value.cbegin(), value.cend(), [](char c) {
			return std::isdigit(static_cast<unsigned char>(c));
		});
	}

	std::string getSteamAppIdFromFile(FileData* game)
	{
		if (game == nullptr || Utils::String::toLower(Utils::FileSystem::getExtension(game->getPath())) != ".steam")
			return "";

		std::ifstream input(game->getPath());
		if (!input.is_open())
			return "";

		std::string line;
		while (std::getline(input, line))
		{
			line = Utils::String::trim(line);
			if (!Utils::String::startsWith(line, "appid="))
				continue;

			auto appId = Utils::String::trim(line.substr(6));
			if (isDigits(appId))
				return appId;
		}

		return "";
	}

	std::string getSteamAppId(FileData* game)
	{
		if (game == nullptr)
			return "";

		auto appId = game->getMetadata(MetaDataId::SteamAppId);
		if (isDigits(appId))
			return appId;

		return getSteamAppIdFromFile(game);
	}

	std::string getCleanGameName(const ScraperSearchParams& params)
	{
		auto cleanName = params.nameOverride;
		if (cleanName.empty())
			cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(params.game->getPath()));

		return Utils::String::trim(cleanName);
	}

	std::string pageUrl(const std::string& page)
	{
		return PCGW_PAGE_URL + HttpReq::urlEncode(Utils::String::replace(page, " ", "_"));
	}

	std::string cleanCompanyList(const std::string& value)
	{
		auto cleaned = Utils::String::replace(value, "Company:", "");
		cleaned = Utils::String::replace(cleaned, ",", ", ");
		cleaned = Utils::String::replace(cleaned, ",  ", ", ");
		return Utils::String::trim(cleaned);
	}

	std::string getPCGamingWikiDescription(const std::string& page, const std::string& url, const std::string& developers, const std::string& publishers)
	{
		if (page.empty())
			return "";

		std::vector<std::string> lines;
		lines.push_back("PCGamingWiki: " + page);
		if (!developers.empty())
			lines.push_back("Developers: " + developers);
		if (!publishers.empty())
			lines.push_back("Publishers: " + publishers);
		if (!url.empty())
			lines.push_back("URL: " + url);

		return Utils::String::join(lines, "\n");
	}

	std::string appendPCGamingWikiDescription(const std::string& description, const std::string& pcgwDescription)
	{
		if (pcgwDescription.empty() || description.find(pcgwDescription) != std::string::npos)
			return description;

		auto trimmed = Utils::String::trim(description);
		if (trimmed.empty())
			return pcgwDescription;

		return trimmed + "\n\n" + pcgwDescription;
	}

	std::string cargoQueryForSteamAppId(const std::string& appId)
	{
		auto where = "Infobox_game.Steam_AppID HOLDS \"" + appId + "\"";
		return PCGW_API_URL + "?action=cargoquery&tables=Infobox_game"
			+ "&fields=Infobox_game._pageName=Page,Infobox_game._pageID=PageID,Infobox_game.Steam_AppID,Infobox_game.Developers,Infobox_game.Publishers"
			+ "&where=" + HttpReq::urlEncode(where)
			+ "&format=json";
	}

	std::string searchQueryForName(const std::string& name)
	{
		return PCGW_API_URL + "?action=query&list=search&srlimit=5&format=json&srsearch=" + HttpReq::urlEncode(name);
	}

	std::string getStringMember(const Value& value, const char* key)
	{
		if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString())
			return "";

		return value[key].GetString();
	}
}

void PCGamingWikiScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	auto appId = getSteamAppId(params.game);
	if (!appId.empty())
	{
		requests.push(std::unique_ptr<ScraperRequest>(new PCGamingWikiRequest(results, params.game, cargoQueryForSteamAppId(appId), PCGamingWikiRequest::SteamAppId)));
		return;
	}

	auto cleanName = getCleanGameName(params);
	if (cleanName.empty())
		return;

	requests.push(std::unique_ptr<ScraperRequest>(new PCGamingWikiRequest(results, params.game, searchQueryForName(cleanName), PCGamingWikiRequest::NameSearch)));
}

bool PCGamingWikiScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& PCGamingWikiScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds = {};
	return mdds;
}

void PCGamingWikiRequest::addResult(std::vector<ScraperSearchResult>& results, const std::string& page, const std::string& pageId, const std::string& developers, const std::string& publishers)
{
	if (page.empty())
		return;

	ScraperSearchResult result("PCGamingWiki");
	if (mGame != nullptr)
		result.mdl = mGame->getMetadata();

	result.mdl.set(MetaDataId::Name, page);
	result.mdl.set(MetaDataId::PCGamingWikiPage, page);
	auto url = pageUrl(page);
	result.mdl.set(MetaDataId::PCGamingWikiUrl, url);

	if (!pageId.empty())
		result.mdl.set(MetaDataId::PCGamingWikiPageId, pageId);

	auto cleanedDevelopers = cleanCompanyList(developers);
	auto cleanedPublishers = cleanCompanyList(publishers);
	if (!cleanedDevelopers.empty())
		result.mdl.set(MetaDataId::PCGamingWikiDevelopers, cleanedDevelopers);
	if (!cleanedPublishers.empty())
		result.mdl.set(MetaDataId::PCGamingWikiPublishers, cleanedPublishers);

	auto pcgwDescription = getPCGamingWikiDescription(page, url, cleanedDevelopers, cleanedPublishers);
	result.mdl.set(MetaDataId::Desc, appendPCGamingWikiDescription(result.mdl.get(MetaDataId::Desc), pcgwDescription));

	results.push_back(result);
}

bool PCGamingWikiRequest::process(const std::string& response, std::vector<ScraperSearchResult>& results)
{
	Document doc;
	doc.Parse(response.c_str());

	if (doc.HasParseError())
	{
		LOG(LogWarning) << "PCGamingWikiRequest - Error parsing JSON: " << GetParseError_En(doc.GetParseError());
		return true;
	}

	if (mLookupMode == SteamAppId)
	{
		if (!doc.HasMember("cargoquery") || !doc["cargoquery"].IsArray())
			return true;

		const Value& data = doc["cargoquery"];
		for (SizeType i = 0; i < data.Size(); i++)
		{
			if (!data[i].IsObject() || !data[i].HasMember("title") || !data[i]["title"].IsObject())
				continue;

			const Value& title = data[i]["title"];
			addResult(results,
				getStringMember(title, "Page"),
				getStringMember(title, "PageID"),
				getStringMember(title, "Developers"),
				getStringMember(title, "Publishers"));
		}

		return true;
	}

	if (!doc.HasMember("query") || !doc["query"].IsObject() || !doc["query"].HasMember("search") || !doc["query"]["search"].IsArray())
		return true;

	const Value& data = doc["query"]["search"];
	for (SizeType i = 0; i < data.Size(); i++)
	{
		const Value& item = data[i];
		addResult(results,
			getStringMember(item, "title"),
			item.HasMember("pageid") && item["pageid"].IsInt() ? std::to_string(item["pageid"].GetInt()) : "",
			"",
			"");
	}

	return true;
}
