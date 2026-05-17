#include "scrapers/SteamGridDBScraper.h"

#include "FileData.h"
#include "Log.h"
#include "Paths.h"
#include "Settings.h"
#include "SystemConf.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <stdexcept>
#include <vector>

using namespace rapidjson;

namespace
{
	const std::string STEAMGRIDDB_API_URL_BASE = "https://www.steamgriddb.com/api/v2";
	const std::string STEAMGRIDDB_API_KEY_FILE_NAME = "steamgriddb.key";

	std::vector<std::string> getSteamGridDBApiKeyFilePaths()
	{
		std::vector<std::string> paths;
		paths.push_back(Utils::FileSystem::combine(Paths::getUserEmulationStationPath(), STEAMGRIDDB_API_KEY_FILE_NAME));
		paths.push_back("/userdata/system/" + STEAMGRIDDB_API_KEY_FILE_NAME);
		return paths;
	}

	std::string unwrapSteamGridDBApiKey(std::string key)
	{
		key = Utils::String::trim(key);
		if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\'')))
			key = Utils::String::trim(key.substr(1, key.size() - 2));

		return key;
	}

	std::string extractSteamGridDBApiKey(const std::string& contents)
	{
		for (auto line : Utils::String::splitAny(contents, "\r\n", true))
		{
			line = Utils::String::trim(line);
			if (line.empty() || Utils::String::startsWith(line, "#") || Utils::String::startsWith(line, ";"))
				continue;

			auto equalPos = line.find('=');
			if (equalPos != std::string::npos)
			{
				auto key = Utils::String::trim(line.substr(0, equalPos));
				if (key == "steamgriddb.api_key" || key == "api_key" || key == "key")
					line = line.substr(equalPos + 1);
				else
					continue;
			}

			line = unwrapSteamGridDBApiKey(line);
			if (!line.empty())
				return line;
		}

		return "";
	}

	std::string getSteamGridDBApiKey()
	{
		auto apiKey = unwrapSteamGridDBApiKey(SystemConf::getInstance()->get("steamgriddb.api_key"));
		if (!apiKey.empty())
			return apiKey;

		for (auto path : getSteamGridDBApiKeyFilePaths())
		{
			if (!Utils::FileSystem::exists(path))
				continue;

			apiKey = extractSteamGridDBApiKey(Utils::FileSystem::readAllText(path));
			if (!apiKey.empty())
				return apiKey;
		}

		return "";
	}

	HttpReqOptions getSteamGridDBOptions()
	{
		HttpReqOptions options;
		options.customHeaders.push_back("Accept: application/json");
		options.customHeaders.push_back("Authorization: Bearer " + getSteamGridDBApiKey());
		return options;
	}

	std::string getSteamAppId(FileData* game)
	{
		if (game == nullptr || Utils::FileSystem::getExtension(game->getPath()) != ".steam")
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

			auto appid = Utils::String::trim(line.substr(6));
			if (!appid.empty() && std::all_of(appid.cbegin(), appid.cend(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
				return appid;
		}

		return "";
	}

	std::string getCleanGameName(const ScraperSearchParams& params)
	{
		auto cleanName = params.nameOverride;
		if (cleanName.empty())
			cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(params.game->getPath()));

		return cleanName;
	}

	std::string addGridFilters(const std::string& url)
	{
		return url + "?dimensions=600x900,342x482,660x930"
			+ "&types=static"
			+ "&mimes=image%2Fpng,image%2Fjpeg"
			+ "&nsfw=false"
			+ "&humor=false"
			+ "&epilepsy=false"
			+ "&limit=1";
	}

	std::string addHeroFilters(const std::string& url)
	{
		return url + "?types=static"
			+ "&mimes=image%2Fpng,image%2Fjpeg"
			+ "&nsfw=false"
			+ "&humor=false"
			+ "&epilepsy=false"
			+ "&limit=1";
	}

	std::string addLogoFilters(const std::string& url)
	{
		return url + "?types=static"
			+ "&mimes=image%2Fpng"
			+ "&nsfw=false"
			+ "&humor=false"
			+ "&epilepsy=false"
			+ "&limit=1";
	}
}

void SteamGridDBScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	if (getSteamGridDBApiKey().empty())
	{
		if (!params.isManualScrape)
			throw std::runtime_error("INVALID CREDENTIALS");

		return;
	}

	auto options = getSteamGridDBOptions();
	auto appid = getSteamAppId(params.game);

	if (!appid.empty())
	{
		auto path = STEAMGRIDDB_API_URL_BASE + "/games/steam/" + HttpReq::urlEncode(appid);
		requests.push(std::unique_ptr<ScraperRequest>(new SteamGridDBRequest(results, path, &options, SteamGridDBRequest::SteamAppId, params.isManualScrape)));
		return;
	}

	auto cleanName = getCleanGameName(params);
	if (cleanName.empty())
		return;

	auto path = STEAMGRIDDB_API_URL_BASE + "/search/autocomplete/" + HttpReq::urlEncode(cleanName);
	requests.push(std::unique_ptr<ScraperRequest>(new SteamGridDBRequest(results, path, &options, SteamGridDBRequest::SearchByName, params.isManualScrape)));
}

bool SteamGridDBScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& SteamGridDBScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds =
	{
		ScraperMediaSource::Box2d,
		ScraperMediaSource::FanArt,
		ScraperMediaSource::Wheel
	};

	return mdds;
}

std::vector<SteamGridDBRequest::Game> SteamGridDBRequest::parseGames(const std::string& response)
{
	std::vector<Game> games;
	Document doc;
	doc.Parse(response.c_str());

	if (doc.HasParseError())
	{
		LOG(LogWarning) << "SteamGridDBRequest - Error parsing JSON: " << GetParseError_En(doc.GetParseError());
		return games;
	}

	if (!doc.HasMember("success") || !doc["success"].IsBool() || !doc["success"].GetBool() || !doc.HasMember("data"))
		return games;

	auto addGame = [&games](const Value& game)
	{
		if (!game.IsObject() || !game.HasMember("id") || !game["id"].IsInt() || !game.HasMember("name") || !game["name"].IsString())
			return;

		Game item;
		item.id = game["id"].GetInt();
		item.name = game["name"].GetString();
		games.push_back(item);
	};

	const Value& data = doc["data"];
	if (mLookupMode == SteamAppId && data.IsObject())
		addGame(data);
	else if (data.IsArray())
	{
		for (SizeType i = 0; i < data.Size() && i < 8; i++)
			addGame(data[i]);
	}

	return games;
}

void SteamGridDBRequest::preProcess(const std::string& response)
{
	mGames = parseGames(response);

	for (auto game : mGames)
	{
		auto id = std::to_string(game.id);
		mDependencyQueue.push({ "grid:" + id, addGridFilters(STEAMGRIDDB_API_URL_BASE + "/grids/game/" + id) });
		mDependencyQueue.push({ "hero:" + id, addHeroFilters(STEAMGRIDDB_API_URL_BASE + "/heroes/game/" + id) });
		mDependencyQueue.push({ "logo:" + id, addLogoFilters(STEAMGRIDDB_API_URL_BASE + "/logos/game/" + id) });
	}
}

std::string SteamGridDBRequest::getAssetUrl(const std::string& dependencyId)
{
	auto response = getDependencyResponse(dependencyId);
	if (response.empty())
		return "";

	Document doc;
	doc.Parse(response.c_str());

	if (doc.HasParseError() || !doc.HasMember("success") || !doc["success"].IsBool() || !doc["success"].GetBool() || !doc.HasMember("data") || !doc["data"].IsArray())
		return "";

	const Value& data = doc["data"];
	if (data.Empty() || !data[0].IsObject() || !data[0].HasMember("url") || !data[0]["url"].IsString())
		return "";

	return data[0]["url"].GetString();
}

bool SteamGridDBRequest::process(const std::string& response, std::vector<ScraperSearchResult>& results)
{
	if (mGames.empty())
		mGames = parseGames(response);

	for (auto game : mGames)
	{
		ScraperSearchResult result("SteamGridDB");
		auto id = std::to_string(game.id);
		auto grid = getAssetUrl("grid:" + id);
		auto hero = getAssetUrl("hero:" + id);
		auto logo = getAssetUrl("logo:" + id);

		result.mdl.set(MetaDataId::Name, game.name);

		auto imageSource = Settings::getInstance()->getString("ScrapperImageSrc");
		if (!imageSource.empty())
		{
			if (imageSource == "fanart" && !hero.empty())
				result.urls[MetaDataId::Image] = ScraperSearchItem(hero);
			else if (!grid.empty())
				result.urls[MetaDataId::Image] = ScraperSearchItem(grid);
		}

		if (!Settings::getInstance()->getString("ScrapperThumbSrc").empty() && !grid.empty())
			result.urls[MetaDataId::Thumbnail] = ScraperSearchItem(grid);

		if (Settings::getInstance()->getBool("ScrapeFanart") && !hero.empty())
			result.urls[MetaDataId::FanArt] = ScraperSearchItem(hero);

		if (!Settings::getInstance()->getString("ScrapperLogoSrc").empty() && !logo.empty())
			result.urls[MetaDataId::Marquee] = ScraperSearchItem(logo);

		if (result.hasMedia())
			results.push_back(result);
	}

	return true;
}
