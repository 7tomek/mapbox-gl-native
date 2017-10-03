#include <mbgl/map/map_snapshotter.hpp>

#include <mbgl/actor/actor_ref.hpp>
#include <mbgl/gl/headless_frontend.hpp>
#include <mbgl/map/map.hpp>
#include <mbgl/storage/file_source.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/util/event.hpp>

namespace mbgl {

class MapSnapshotter::Impl {
public:
    Impl(FileSource&,
         Scheduler&,
         const std::string& styleURL,
         const Size&,
         const float pixelRatio,
         const CameraOptions&,
         const optional<LatLngBounds> region,
         const optional<std::string> programCacheDir);

    void setStyle(std::string styleURL);
    void setSize(Size);
    void setCameraOptions(CameraOptions);
    void setRegion(LatLngBounds);

    void snapshot(ActorRef<MapSnapshotter::Callback>);

private:
    HeadlessFrontend frontend;
    Map map;
};

MapSnapshotter::Impl::Impl(FileSource& fileSource,
           Scheduler& scheduler,
           const std::string& styleURL,
           const Size& size,
           const float pixelRatio,
           const CameraOptions& cameraOptions,
           const optional<LatLngBounds> region,
           const optional<std::string> programCacheDir)
    : frontend(size, pixelRatio, fileSource, scheduler, programCacheDir)
    , map(frontend, MapObserver::nullObserver(), size, pixelRatio, fileSource, scheduler, MapMode::Still) {

    map.getStyle().loadURL(styleURL);

    map.jumpTo(cameraOptions);

    // Set region, if specified
    if (region) {
        this->setRegion(*region);
    }
}

void MapSnapshotter::Impl::snapshot(ActorRef<MapSnapshotter::Callback> callback) {
    map.renderStill([this, callback = std::move(callback)] (std::exception_ptr error) mutable {
        callback.invoke(&MapSnapshotter::Callback::operator(), error, error ? PremultipliedImage() : frontend.readStillImage());
    });
}

void MapSnapshotter::Impl::setStyle(std::string styleURL) {
    map.getStyle().loadURL(styleURL);
}

void MapSnapshotter::Impl::setSize(Size size) {
    map.setSize(size);
    frontend.setSize(size);
}

void MapSnapshotter::Impl::setCameraOptions(CameraOptions cameraOptions) {
    map.jumpTo(cameraOptions);
}

void MapSnapshotter::Impl::setRegion(LatLngBounds region) {
    mbgl::EdgeInsets insets = { 0, 0, 0, 0 };
    std::vector<LatLng> latLngs = { region.southwest(), region.northeast() };
    map.jumpTo(map.cameraForLatLngs(latLngs, insets));
}

MapSnapshotter::MapSnapshotter(FileSource& fileSource,
                               Scheduler& scheduler,
                               const std::string& styleURL,
                               const Size& size,
                               const float pixelRatio,
                               const CameraOptions& cameraOptions,
                               const optional<LatLngBounds> region,
                               const optional<std::string> programCacheDir)
   : impl(std::make_unique<util::Thread<MapSnapshotter::Impl>>("Map Snapshotter", fileSource, scheduler, styleURL, size, pixelRatio, cameraOptions, region, programCacheDir)) {
}

MapSnapshotter::~MapSnapshotter() = default;

void MapSnapshotter::snapshot(ActorRef<MapSnapshotter::Callback> callback) {
    impl->actor().invoke(&Impl::snapshot, std::move(callback));
}

void MapSnapshotter::setStyle(const std::string& styleURL) {
    impl->actor().invoke(&Impl::setStyle, styleURL);
}

void MapSnapshotter::setSize(const Size& size) {
    impl->actor().invoke(&Impl::setSize, size);
}

void MapSnapshotter::setCameraOptions(const CameraOptions& options) {
    impl->actor().invoke(&Impl::setCameraOptions, options);
}

void MapSnapshotter::setRegion(const LatLngBounds& bounds) {
    impl->actor().invoke(&Impl::setRegion, std::move(bounds));
}

} // namespace mbgl
