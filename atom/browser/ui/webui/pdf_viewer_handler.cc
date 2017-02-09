// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/ui/webui/pdf_viewer_handler.h"

#include "atom/browser/atom_browser_context.h"
#include "atom/browser/stream_manager.h"
#include "base/values.h"
#include "content/public/browser/stream_handle.h"
#include "content/public/browser/stream_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/page_zoom.h"
#include "net/http/http_response_headers.h"

namespace atom {

namespace {

void CreateResponseHeadersDictionary(const net::HttpResponseHeaders* headers,
                                     base::DictionaryValue* result) {
  if (!headers)
    return;

  size_t iter = 0;
  std::string header_name;
  std::string header_value;
  while (headers->EnumerateHeaderLines(&iter, &header_name, &header_value)) {
    base::Value* existing_value = nullptr;
    if (result->Get(header_name, &existing_value)) {
      base::StringValue* existing_string_value =
          static_cast<base::StringValue*>(existing_value);
      existing_string_value->GetString()->append(", ").append(header_value);
    } else {
      result->SetString(header_name, header_value);
    }
  }
}

}  // namespace

PdfViewerHandler::PdfViewerHandler(const std::string& view_id)
    : view_id_(view_id) {}

PdfViewerHandler::~PdfViewerHandler() {}

void PdfViewerHandler::RegisterMessages() {
  auto browser_context = static_cast<AtomBrowserContext*>(
      web_ui()->GetWebContents()->GetBrowserContext());
  auto stream_manager = browser_context->stream_manager();
  stream_ = stream_manager->ReleaseStream(view_id_);

  web_ui()->RegisterMessageCallback(
      "initialize",
      base::Bind(&PdfViewerHandler::Initialize, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDefaultZoom",
      base::Bind(&PdfViewerHandler::GetInitialZoom, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getInitialZoom",
      base::Bind(&PdfViewerHandler::GetInitialZoom, base::Unretained(this)));
}

void PdfViewerHandler::OnJavascriptAllowed() {
  auto host_zoom_map =
      content::HostZoomMap::GetForWebContents(web_ui()->GetWebContents());
  host_zoom_map_subscription_ =
      host_zoom_map->AddZoomLevelChangedCallback(base::Bind(
          &PdfViewerHandler::OnZoomLevelChanged, base::Unretained(this)));
}

void PdfViewerHandler::OnJavascriptDisallowed() {
  host_zoom_map_subscription_.reset();
}

void PdfViewerHandler::Initialize(const base::ListValue* args) {
  AllowJavascript();

  CHECK_EQ(1U, args->GetSize());
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));
  std::unique_ptr<base::DictionaryValue> stream_info(new base::DictionaryValue);
  auto stream_url = stream_->handle->GetURL().spec();
  auto original_url = stream_->original_url.spec();
  stream_info->SetString("streamURL", stream_url);
  stream_info->SetString("originalURL", original_url);
  std::unique_ptr<base::DictionaryValue> headers_dict(
      new base::DictionaryValue);
  CreateResponseHeadersDictionary(stream_->response_headers.get(),
                                  headers_dict.get());
  stream_info->Set("responseHeaders", std::move(headers_dict));
  ResolveJavascriptCallback(*callback_id, *stream_info);
}

void PdfViewerHandler::GetDefaultZoom(const base::ListValue* args) {
  if (!IsJavascriptAllowed())
    return;
  CHECK_EQ(1U, args->GetSize());
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));

  auto host_zoom_map =
      content::HostZoomMap::GetForWebContents(web_ui()->GetWebContents());
  double zoom_level = host_zoom_map->GetDefaultZoomLevel();
  ResolveJavascriptCallback(
      *callback_id,
      base::FundamentalValue(content::ZoomLevelToZoomFactor(zoom_level)));
}

void PdfViewerHandler::GetInitialZoom(const base::ListValue* args) {
  if (!IsJavascriptAllowed())
    return;
  CHECK_EQ(1U, args->GetSize());
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));

  double zoom_level =
      content::HostZoomMap::GetZoomLevel(web_ui()->GetWebContents());
  ResolveJavascriptCallback(
      *callback_id,
      base::FundamentalValue(content::ZoomLevelToZoomFactor(zoom_level)));
}

void PdfViewerHandler::OnZoomLevelChanged(
    const content::HostZoomMap::ZoomLevelChange& change) {
  // TODO(deepak1556): This will work only if zoom level is changed through host
  // zoom map.
  if (change.scheme == "chrome" && change.host == "pdf-viewer") {
    CallJavascriptFunction(
        "cr.webUIListenerCallback", base::StringValue("onZoomLevelChanged"),
        base::FundamentalValue(
            content::ZoomLevelToZoomFactor(change.zoom_level)));
  }
}

}  // namespace atom
