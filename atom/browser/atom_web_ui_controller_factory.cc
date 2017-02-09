// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/atom_web_ui_controller_factory.h"

#include "base/strings/string_util.h"
#include "base/strings/string_split.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/common/bindings_policy.h"
#include "grit/pdf_viewer_resources_map.h"
#include "ui/base/resource/resource_bundle.h"

namespace atom {

namespace {

const char kChromeUIPdfViewerHost[] = "pdf-viewer";

std::string PathWithoutParams(const std::string& path) {
  return GURL(std::string("chrome://pdf-viewer/") + path).path().substr(1);
}

std::string GetMimeTypeForPath(const std::string& path) {
  std::string filename = PathWithoutParams(path);
  if (base::EndsWith(filename, ".html", base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/html";
  } else if (base::EndsWith(filename, ".css",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/css";
  } else if (base::EndsWith(filename, ".js",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "application/javascript";
  } else if (base::EndsWith(filename, ".png",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/png";
  } else if (base::EndsWith(filename, ".gif",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/gif";
  } else if (base::EndsWith(filename, ".svg",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/svg+xml";
  } else if (base::EndsWith(filename, ".manifest",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/cache-manifest";
  }
  return "text/html";
}

class BundledDataSource : public content::URLDataSource {
 public:
  BundledDataSource() {
    for (size_t i = 0; i < kPdfViewerResourcesSize; ++i) {
      base::FilePath resource_path =
          base::FilePath().AppendASCII(kPdfViewerResources[i].name);
      resource_path = resource_path.NormalizePathSeparators();

      DCHECK(path_to_resource_id_.find(resource_path) ==
             path_to_resource_id_.end());
      path_to_resource_id_[resource_path] = kPdfViewerResources[i].value;
    }
  }

  // content::URLDataSource implementation.
  std::string GetSource() const override { return kChromeUIPdfViewerHost; }

  void StartDataRequest(const std::string& path,
                        int render_process_id,
                        int render_frame_id,
                        const GotDataCallback& callback) override {
    std::string filename = PathWithoutParams(path);
    std::map<base::FilePath, int>::const_iterator entry =
        path_to_resource_id_.find(base::FilePath(filename));
    if (entry != path_to_resource_id_.end()) {
      int resource_id = entry->second;
      const ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
      callback.Run(rb.LoadDataResourceBytes(resource_id));
    }
  }

  std::string GetMimeType(const std::string& path) const override {
    return GetMimeTypeForPath(path);
  }

  bool ShouldAddContentSecurityPolicy() const override { return false; }

  bool ShouldDenyXFrameOptions() const override { return false; }

  bool ShouldServeMimeTypeAsContentTypeHeader() const override { return true; }

 private:
  ~BundledDataSource() override {}

  // A map from a resource path to the resource ID.
  std::map<base::FilePath, int> path_to_resource_id_;

  DISALLOW_COPY_AND_ASSIGN(BundledDataSource);
};

class PdfViewerUI : public content::WebUIController {
 public:
  PdfViewerUI(content::BrowserContext* browser_context,
              content::WebUI* web_ui,
              const std::string& stream_url,
              const std::string& original_url)
      : content::WebUIController(web_ui),
        stream_url_(stream_url),
        original_url_(original_url) {
    web_ui->RegisterMessageCallback(
        "initialize",
        base::Bind(&PdfViewerUI::OnInitialize, base::Unretained(this)));
    content::URLDataSource::Add(browser_context, new BundledDataSource);
  }

  void RenderViewCreated(content::RenderViewHost* rvh) override {
    rvh->AllowBindings(content::BINDINGS_POLICY_WEB_UI);
  }

  void OnInitialize(const base::ListValue* args) {
    web_ui()->CallJavascriptFunctionUnsafe("main",
                                           base::StringValue(original_url_),
                                           base::StringValue(original_url_));
  }

 private:
  std::string stream_url_;
  std::string original_url_;

  DISALLOW_COPY_AND_ASSIGN(PdfViewerUI);
};
}

// static
AtomWebUIControllerFactory* AtomWebUIControllerFactory::GetInstance() {
  return base::Singleton<AtomWebUIControllerFactory>::get();
}

AtomWebUIControllerFactory::AtomWebUIControllerFactory() {}

AtomWebUIControllerFactory::~AtomWebUIControllerFactory() {}

content::WebUI::TypeID AtomWebUIControllerFactory::GetWebUIType(
    content::BrowserContext* browser_context,
    const GURL& url) const {
  if (url.host() == kChromeUIPdfViewerHost) {
    return const_cast<AtomWebUIControllerFactory*>(this);
  }

  return content::WebUI::kNoWebUI;
}

bool AtomWebUIControllerFactory::UseWebUIForURL(
    content::BrowserContext* browser_context,
    const GURL& url) const {
  return GetWebUIType(browser_context, url) != content::WebUI::kNoWebUI;
}

bool AtomWebUIControllerFactory::UseWebUIBindingsForURL(
    content::BrowserContext* browser_context,
    const GURL& url) const {
  return UseWebUIForURL(browser_context, url);
}

content::WebUIController*
AtomWebUIControllerFactory::CreateWebUIControllerForURL(content::WebUI* web_ui,
                                                        const GURL& url) const {
  if (url.host() == kChromeUIPdfViewerHost) {
    base::StringPairs toplevel_params;
    base::SplitStringIntoKeyValuePairs(url.query(), '=', '&', &toplevel_params);
    std::string stream_url, original_url;
    for (const auto& param : toplevel_params) {
      if (param.first == "streamURL") {
        stream_url = param.second;
      } else if (param.first == "originalURL") {
        original_url = param.second;
      }
    }
    auto browser_context = web_ui->GetWebContents()->GetBrowserContext();
    return new PdfViewerUI(browser_context, web_ui, stream_url, original_url);
  }
  return nullptr;
}

}  // namespace atom
