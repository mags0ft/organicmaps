#include "com/mapswithme/maps/SearchEngine.hpp"
#include "com/mapswithme/maps/Framework.hpp"
#include "com/mapswithme/maps/UserMarkHelper.hpp"
#include "com/mapswithme/platform/Platform.hpp"

#include "map/bookmarks_search_params.hpp"
#include "map/everywhere_search_params.hpp"
#include "map/place_page_info.hpp"
#include "map/viewport_search_params.hpp"

#include "search/mode.hpp"
#include "search/result.hpp"

#include "platform/network_policy.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include "defines.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

using namespace std;
using namespace std::placeholders;
using search::Result;
using search::Results;

namespace
{
FeatureID const kEmptyFeatureId;

// This cache is needed only for showing a specific result on the map after click on the list item.
// Don't use it with another intentions!
Results g_results;

// Timestamp of last search query. Results with older stamps are ignored.
uint64_t g_queryTimestamp;
// Implements 'NativeSearchListener' java interface.
jobject g_javaListener;
jmethodID g_updateResultsId;
jmethodID g_endResultsId;
// Cached classes and methods to return results.
jclass g_resultClass;
jmethodID g_resultConstructor;
jmethodID g_suggestConstructor;
jclass g_descriptionClass;
jmethodID g_descriptionConstructor;
jclass g_popularityClass;
jmethodID g_popularityConstructor;

// Implements 'NativeMapSearchListener' java interface.
jmethodID g_mapResultsMethod;
jclass g_mapResultClass;
jmethodID g_mapResultCtor;

jmethodID g_updateBookmarksResultsId;
jmethodID g_endBookmarksResultsId;

bool PopularityHasHigherPriority(bool hasPosition, double distanceInMeters)
{
  return !hasPosition || distanceInMeters > search::Result::kPopularityHighPriorityMinDistance;
}

jobject ToJavaResult(Result & result, search::ProductInfo const & productInfo, bool hasPosition,
                     double lat, double lon)
{
  JNIEnv * env = jni::GetEnv();

  jni::TScopedLocalIntArrayRef ranges(
      env, env->NewIntArray(static_cast<jsize>(result.GetHighlightRangesCount() * 2)));
  jint * rawArr = env->GetIntArrayElements(ranges, nullptr);
  for (int i = 0; i < result.GetHighlightRangesCount(); i++)
  {
    auto const & range = result.GetHighlightRange(i);
    rawArr[2 * i] = range.first;
    rawArr[2 * i + 1] = range.second;
  }
  env->ReleaseIntArrayElements(ranges.get(), rawArr, 0);

  ms::LatLon ll = ms::LatLon::Zero();
  string distance;
  double distanceInMeters = 0.0;

  if (result.HasPoint())
  {
    auto const center = result.GetFeatureCenter();
    ll = mercator::ToLatLon(center);
    if (hasPosition)
    {
      distanceInMeters = ms::DistanceOnEarth(lat, lon,
                                             mercator::YToLat(center.y),
                                             mercator::XToLon(center.x));
      distance = measurement_utils::FormatDistance(distanceInMeters);
    }
  }

  bool popularityHasHigherPriority = PopularityHasHigherPriority(hasPosition, distanceInMeters);

  if (result.IsSuggest())
  {
    jni::TScopedLocalRef name(env, jni::ToJavaString(env, result.GetString()));
    jni::TScopedLocalRef suggest(env, jni::ToJavaString(env, result.GetSuggestionString()));
    jobject ret = env->NewObject(g_resultClass, g_suggestConstructor, name.get(), suggest.get(), ll.m_lat, ll.m_lon, ranges.get());
    ASSERT(ret, ());
    return ret;
  }

  auto const isFeature = result.GetResultType() == Result::Type::Feature;
  jni::TScopedLocalRef featureId(env, usermark_helper::CreateFeatureId(env, isFeature ?
                                                                            result.GetFeatureID() :
                                                                            kEmptyFeatureId));
  string readableType = isFeature ? classif().GetReadableObjectName(result.GetFeatureType()) : "";

  jni::TScopedLocalRef featureType(env, jni::ToJavaString(env, readableType));
  jni::TScopedLocalRef address(env, jni::ToJavaString(env, result.GetAddress()));
  jni::TScopedLocalRef dist(env, jni::ToJavaString(env, distance));
  jni::TScopedLocalRef cuisine(env, jni::ToJavaString(env, result.GetCuisine()));
  jni::TScopedLocalRef brand(env, jni::ToJavaString(env, result.GetBrand()));
  jni::TScopedLocalRef airportIata(env, jni::ToJavaString(env, result.GetAirportIata()));
  jni::TScopedLocalRef roadShields(env, jni::ToJavaString(env, result.GetRoadShields()));


  jni::TScopedLocalRef desc(env, env->NewObject(g_descriptionClass, g_descriptionConstructor,
                                                featureId.get(), featureType.get(), address.get(),
                                                dist.get(), cuisine.get(), brand.get(), airportIata.get(),
                                                roadShields.get(),
                                                static_cast<jint>(result.IsOpenNow()),
                                                static_cast<jboolean>(popularityHasHigherPriority)));

  jni::TScopedLocalRef name(env, jni::ToJavaString(env, result.GetString()));
  jni::TScopedLocalRef popularity(env, env->NewObject(g_popularityClass,
                                                      g_popularityConstructor,
                                                      static_cast<jint>(result.GetRankingInfo().m_popularity)));
  jobject ret =
      env->NewObject(g_resultClass, g_resultConstructor, name.get(), desc.get(), ll.m_lat, ll.m_lon,
                     ranges.get(), result.IsHotel(), popularity.get());
  ASSERT(ret, ());

  return ret;
}

void OnResults(Results const & results, vector<search::ProductInfo> const & productInfo,
               long long timestamp, bool isMapAndTable, bool hasPosition, double lat, double lon)
{
  // Ignore results from obsolete searches.
  if (g_queryTimestamp > timestamp)
    return;

  JNIEnv * env = jni::GetEnv();

  if (!results.IsEndMarker() || results.IsEndedNormal())
  {
    jni::TScopedLocalObjectArrayRef jResults(
        env, BuildSearchResults(results, productInfo, hasPosition, lat, lon));
    env->CallVoidMethod(g_javaListener, g_updateResultsId, jResults.get(),
                        static_cast<jlong>(timestamp));
  }

  if (results.IsEndMarker())
  {
    env->CallVoidMethod(g_javaListener, g_endResultsId, static_cast<jlong>(timestamp));
    if (isMapAndTable && results.IsEndedNormal())
      g_framework->NativeFramework()->GetSearchAPI().PokeSearchInViewport();
  }
}

jobjectArray BuildJavaMapResults(vector<storage::DownloaderSearchResult> const & results)
{
  JNIEnv * env = jni::GetEnv();

  auto const count = static_cast<jsize>(results.size());
  jobjectArray const res = env->NewObjectArray(count, g_mapResultClass, nullptr);
  for (jsize i = 0; i < count; i++)
  {
    jni::TScopedLocalRef country(env, jni::ToJavaString(env, results[i].m_countryId));
    jni::TScopedLocalRef matched(env, jni::ToJavaString(env, results[i].m_matchedName));
    jni::TScopedLocalRef item(env, env->NewObject(g_mapResultClass, g_mapResultCtor, country.get(), matched.get()));
    env->SetObjectArrayElement(res, i, item.get());
  }

  return res;
}

void OnMapSearchResults(storage::DownloaderSearchResults const & results, long long timestamp)
{
  // Ignore results from obsolete searches.
  if (g_queryTimestamp > timestamp)
    return;

  JNIEnv * env = jni::GetEnv();
  jni::TScopedLocalObjectArrayRef jResults(env, BuildJavaMapResults(results.m_results));
  env->CallVoidMethod(g_javaListener, g_mapResultsMethod, jResults.get(),
                      static_cast<jlong>(timestamp), results.m_endMarker);
}

void OnBookmarksSearchStarted()
{
  // Dummy.
}

void OnBookmarksSearchResults(search::BookmarksSearchParams::Results const & results,
                              search::BookmarksSearchParams::Status status, long long timestamp)
{
  // Ignore results from obsolete searches.
  if (g_queryTimestamp > timestamp)
    return;

  JNIEnv * env = jni::GetEnv();

  auto filteredResults = results;
  g_framework->NativeFramework()->GetBookmarkManager().FilterInvalidBookmarks(filteredResults);
  jni::ScopedLocalRef<jlongArray> jResults(
      env, env->NewLongArray(static_cast<jsize>(filteredResults.size())));
  vector<jlong> const tmp(filteredResults.cbegin(), filteredResults.cend());
  env->SetLongArrayRegion(jResults.get(), 0, static_cast<jsize>(tmp.size()), tmp.data());

  auto const method = (status == search::BookmarksSearchParams::Status::InProgress) ?
                      g_updateBookmarksResultsId : g_endBookmarksResultsId;

  env->CallVoidMethod(g_javaListener, method, jResults.get(), static_cast<jlong>(timestamp));
}
}  // namespace

jobjectArray BuildSearchResults(Results const & results,
                                vector<search::ProductInfo> const & productInfo, bool hasPosition,
                                double lat, double lon)
{
  JNIEnv * env = jni::GetEnv();

  g_results = results;

  auto const count = static_cast<jsize>(g_results.GetCount());
  jobjectArray const jResults = env->NewObjectArray(count, g_resultClass, nullptr);

  for (jsize i = 0; i < count; i++)
  {
    jni::TScopedLocalRef jRes(env,
                              ToJavaResult(g_results[i], productInfo[i], hasPosition, lat, lon));
    env->SetObjectArrayElement(jResults, i, jRes.get());
  }
  return jResults;
}

extern "C"
{
  JNIEXPORT void JNICALL
  Java_com_mapswithme_maps_search_SearchEngine_nativeInit(JNIEnv * env, jobject thiz)
  {
    g_javaListener = env->NewGlobalRef(thiz);
    // public void onResultsUpdate(@NonNull SearchResult[] results, long timestamp)
    g_updateResultsId = jni::GetMethodID(env, g_javaListener, "onResultsUpdate",
                                         "([Lcom/mapswithme/maps/search/SearchResult;J)V");
    // public void onResultsEnd(long timestamp)
    g_endResultsId = jni::GetMethodID(env, g_javaListener, "onResultsEnd", "(J)V");
    g_resultClass = jni::GetGlobalClassRef(env, "com/mapswithme/maps/search/SearchResult");
    g_resultConstructor = jni::GetConstructorID(
        env, g_resultClass,
        "(Ljava/lang/String;Lcom/mapswithme/maps/search/SearchResult$Description;DD[IZ"
          "Lcom/mapswithme/maps/search/Popularity;)V");
    g_suggestConstructor = jni::GetConstructorID(env, g_resultClass, "(Ljava/lang/String;Ljava/lang/String;DD[I)V");
    g_descriptionClass = jni::GetGlobalClassRef(env, "com/mapswithme/maps/search/SearchResult$Description");
    /*
        Description(FeatureId featureId, String featureType, String region, String distance,
                    String cuisine, String brand, String airportIata, String roadShields,
                    int openNow, boolean hasPopularityHigherPriority)
    */
    g_descriptionConstructor = jni::GetConstructorID(env, g_descriptionClass,
                                                     "(Lcom/mapswithme/maps/bookmarks/data/FeatureId;"
                                                     "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                                                     "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                                                     "Ljava/lang/String;IZ)V");

    g_popularityClass = jni::GetGlobalClassRef(env, "com/mapswithme/maps/search/Popularity");
    g_popularityConstructor = jni::GetConstructorID(env, g_popularityClass, "(I)V");

    g_mapResultsMethod = jni::GetMethodID(env, g_javaListener, "onMapSearchResults",
                                          "([Lcom/mapswithme/maps/search/NativeMapSearchListener$Result;JZ)V");
    g_mapResultClass = jni::GetGlobalClassRef(env, "com/mapswithme/maps/search/NativeMapSearchListener$Result");
    g_mapResultCtor = jni::GetConstructorID(env, g_mapResultClass, "(Ljava/lang/String;Ljava/lang/String;)V");

    g_updateBookmarksResultsId =
      jni::GetMethodID(env, g_javaListener, "onBookmarkSearchResultsUpdate", "([JJ)V");
    g_endBookmarksResultsId =
      jni::GetMethodID(env, g_javaListener, "onBookmarkSearchResultsEnd", "([JJ)V");
  }

  JNIEXPORT jboolean JNICALL Java_com_mapswithme_maps_search_SearchEngine_nativeRunSearch(
      JNIEnv * env, jclass clazz, jbyteArray bytes, jboolean isCategory,
      jstring lang, jlong timestamp, jboolean hasPosition, jdouble lat, jdouble lon)
  {
    search::EverywhereSearchParams params;
    params.m_query = jni::ToNativeString(env, bytes);
    params.m_inputLocale = jni::ToNativeString(env, lang);
    params.m_isCategory = isCategory;
    params.m_onResults = bind(&OnResults, _1, _2, timestamp, false, hasPosition, lat, lon);
    bool const searchStarted = g_framework->NativeFramework()->GetSearchAPI().SearchEverywhere(params);
    if (searchStarted)
      g_queryTimestamp = timestamp;
    return searchStarted;
  }

  JNIEXPORT void JNICALL Java_com_mapswithme_maps_search_SearchEngine_nativeRunInteractiveSearch(
      JNIEnv * env, jclass clazz, jbyteArray bytes, jboolean isCategory,
      jstring lang, jlong timestamp, jboolean isMapAndTable)
  {
    search::ViewportSearchParams vparams;
    vparams.m_query = jni::ToNativeString(env, bytes);
    vparams.m_inputLocale = jni::ToNativeString(env, lang);
    vparams.m_isCategory = isCategory;

    // TODO (@alexzatsepin): set up vparams.m_onCompleted here and use
    // HotelsClassifier for hotel queries detection.
    g_framework->NativeFramework()->GetSearchAPI().SearchInViewport(vparams);

    if (isMapAndTable)
    {
      search::EverywhereSearchParams eparams;
      eparams.m_query = vparams.m_query;
      eparams.m_inputLocale = vparams.m_inputLocale;
      eparams.m_onResults = bind(&OnResults, _1, _2, timestamp, isMapAndTable,
                                 false /* hasPosition */, 0.0 /* lat */, 0.0 /* lon */);

      if (g_framework->NativeFramework()->GetSearchAPI().SearchEverywhere(eparams))
        g_queryTimestamp = timestamp;
    }
  }

  JNIEXPORT void JNICALL Java_com_mapswithme_maps_search_SearchEngine_nativeRunSearchMaps(
      JNIEnv * env, jclass clazz, jbyteArray bytes, jstring lang, jlong timestamp)
  {
    storage::DownloaderSearchParams params;
    params.m_query = jni::ToNativeString(env, bytes);
    params.m_inputLocale = jni::ToNativeString(env, lang);
    params.m_onResults = bind(&OnMapSearchResults, _1, timestamp);

    if (g_framework->NativeFramework()->GetSearchAPI().SearchInDownloader(params))
      g_queryTimestamp = timestamp;
  }

  JNIEXPORT jboolean JNICALL Java_com_mapswithme_maps_search_SearchEngine_nativeRunSearchInBookmarks(
      JNIEnv * env, jclass clazz, jbyteArray query, jlong catId, jlong timestamp)
  {
    search::BookmarksSearchParams params;
    params.m_query = jni::ToNativeString(env, query);
    params.m_groupId = static_cast<kml::MarkGroupId>(catId);
    params.m_onStarted = bind(&OnBookmarksSearchStarted);
    params.m_onResults = bind(&OnBookmarksSearchResults, _1, _2, timestamp);

    bool const searchStarted = g_framework->NativeFramework()->GetSearchAPI().SearchInBookmarks(params);
    if (searchStarted)
      g_queryTimestamp = timestamp;
    return searchStarted;
  }

  JNIEXPORT void JNICALL
  Java_com_mapswithme_maps_search_SearchEngine_nativeShowResult(JNIEnv * env, jclass clazz, jint index)
  {
    g_framework->NativeFramework()->ShowSearchResult(g_results[index]);
  }

  JNIEXPORT void JNICALL
  Java_com_mapswithme_maps_search_SearchEngine_nativeCancelInteractiveSearch(JNIEnv * env, jclass clazz)
  {
    g_framework->NativeFramework()->GetSearchAPI().CancelSearch(search::Mode::Viewport);
  }

  JNIEXPORT void JNICALL
  Java_com_mapswithme_maps_search_SearchEngine_nativeCancelEverywhereSearch(JNIEnv * env, jclass clazz)
  {
    g_framework->NativeFramework()->GetSearchAPI().CancelSearch(search::Mode::Everywhere);
  }

  JNIEXPORT void JNICALL
  Java_com_mapswithme_maps_search_SearchEngine_nativeCancelAllSearches(JNIEnv * env, jclass clazz)
  {
    g_framework->NativeFramework()->GetSearchAPI().CancelAllSearches();
  }
} // extern "C"
