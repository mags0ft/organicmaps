class MockRecentlyDeletedCategoriesManager: NSObject, RecentlyDeletedCategoriesManager {

  func add(_ observer: any BookmarksObserver) {}
  
  func remove(_ observer: any BookmarksObserver) {}
  
  var categories = [RecentlyDeletedCategory]()

  func getRecentlyDeletedCategories() -> [RecentlyDeletedCategory] {
    categories
  }

  func deleteFile(at urls: [URL]) {
    categories.removeAll { urls.contains($0.fileURL) }
  }

  func deleteAllRecentlyDeletedCategories() {
    categories.removeAll()
  }

  func recoverRecentlyDeletedCategories(at urls: [URL]) {
    categories.removeAll { urls.contains($0.fileURL) }
  }

  func deleteRecentlyDeletedCategory(at urls: [URL]) {
    categories.removeAll { urls.contains($0.fileURL) }
  }
}
