// asuka/maps on Android: a Google Maps MapView, its lifecycle following
// its window. It needs the app's key (asuka.toml's android.mapsKey, which
// the CLI puts in the manifest); without one it says so where the map
// would be, and reports error.
package asuka.maps

import android.content.pm.PackageManager
import android.graphics.Color
import android.widget.FrameLayout
import android.widget.TextView
import com.google.android.gms.maps.CameraUpdateFactory
import com.google.android.gms.maps.GoogleMap
import com.google.android.gms.maps.MapView as GoogleMapView
import com.google.android.gms.maps.model.BitmapDescriptorFactory
import com.google.android.gms.maps.model.LatLng
import com.google.android.gms.maps.model.LatLngBounds
import com.google.android.gms.maps.model.Marker as GoogleMarker
import com.google.android.gms.maps.model.MarkerOptions
import dev.asuka.AsukaModules
import android.view.View as AndroidView

private class MapsView(private val events: ViewEvents) : View {
    private val context = events.context
    private val map = GoogleMapView(context)
    private val box = FrameLayout(context)
    /** The Maps SDK's key, which it throws without (asuka.toml's android.mapsKey). */
    private val hasKey = runCatching {
        context.packageManager.getApplicationInfo(context.packageName, PackageManager.GET_META_DATA)
            .metaData?.getString("com.google.android.geo.API_KEY").isNullOrEmpty().not()
    }.getOrDefault(false)
    override val view: AndroidView get() = box
    private var google: GoogleMap? = null
    private var region = Region()
    private var markers: List<Marker> = emptyList()
    private var type = MapType.STANDARD
    private var showsUser = false
    private var interactive = true
    private var userMoved = false
    private val ids = HashMap<GoogleMarker, String>()

    init {
        if (hasKey) start() else {
            // No key: say so where the map would be.
            box.addView(TextView(context).apply {
                text = "Google Maps needs an API key: set asuka.toml's android.mapsKey"
                gravity = android.view.Gravity.CENTER
                setPadding(32, 32, 32, 32)
            }, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
            box.post { events.error("no Google Maps API key (asuka.toml's android.mapsKey)") }
        }
    }

    private fun start() {
        map.onCreate(null)
        box.addView(map, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        box.addOnAttachStateChangeListener(object : AndroidView.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(v: AndroidView) { map.onStart(); map.onResume() }
            override fun onViewDetachedFromWindow(v: AndroidView) { map.onPause(); map.onStop() }
        })
        map.getMapAsync { g ->
            google = g
            g.setOnCameraMoveStartedListener { reason -> userMoved = reason == GoogleMap.OnCameraMoveStartedListener.REASON_GESTURE }
            g.setOnCameraIdleListener {
                if (!userMoved) return@setOnCameraIdleListener
                userMoved = false
                val b = g.projection.visibleRegion.latLngBounds
                val c = g.cameraPosition.target
                events.regionChange(Region(center = Coordinate(lat = c.latitude, lng = c.longitude),
                    latDelta = b.northeast.latitude - b.southwest.latitude, lngDelta = b.northeast.longitude - b.southwest.longitude))
            }
            g.setOnMarkerClickListener { m -> ids[m]?.let { events.markerPress(it) }; false }
            g.setOnMapClickListener { p -> events.press(Coordinate(lat = p.latitude, lng = p.longitude)) }
            apply(g, moveCamera = true)
        }
    }

    override fun setRegion(value: Region) { region = value; google?.let { moveTo(it, animated = true) } }
    override fun setMarkers(value: List<Marker>) { markers = value; google?.let { placeMarkers(it) } }
    override fun setMapType(value: MapType) { type = value; google?.let { apply(it, false) } }
    override fun setShowsUser(value: Boolean) { showsUser = value; google?.let { apply(it, false) } }
    override fun setInteractive(value: Boolean) { interactive = value; google?.let { apply(it, false) } }

    private fun apply(g: GoogleMap, moveCamera: Boolean) {
        g.mapType = when (type) { MapType.SATELLITE -> GoogleMap.MAP_TYPE_SATELLITE; MapType.HYBRID -> GoogleMap.MAP_TYPE_HYBRID; else -> GoogleMap.MAP_TYPE_NORMAL }
        g.uiSettings.setAllGesturesEnabled(interactive)
        // The user's location needs the permission (asuka/location's); without it, none.
        try { g.isMyLocationEnabled = showsUser } catch (_: SecurityException) {}
        placeMarkers(g)
        if (moveCamera) moveTo(g, animated = false)
    }

    private fun moveTo(g: GoogleMap, animated: Boolean) {
        val r = region
        if (r.latDelta <= 0 && r.lngDelta <= 0 && r.center.lat == 0.0 && r.center.lng == 0.0) return
        val lat = r.center.lat
        val lng = r.center.lng
        val dLat = maxOf(if (r.latDelta > 0) r.latDelta else 0.05, 0.0005) / 2
        val dLng = maxOf(if (r.lngDelta > 0) r.lngDelta else 0.05, 0.0005) / 2
        val bounds = LatLngBounds(LatLng(lat - dLat, lng - dLng), LatLng(lat + dLat, lng + dLng))
        // The region the user moved to, given back: already there.
        val now = g.cameraPosition.target
        if (Math.abs(now.latitude - lat) < 1e-6 && Math.abs(now.longitude - lng) < 1e-6) return
        val move = { if (animated) g.animateCamera(CameraUpdateFactory.newLatLngBounds(bounds, 0)) else g.moveCamera(CameraUpdateFactory.newLatLngBounds(bounds, 0)) }
        // Bounds need the map's size: once it's laid out.
        if (map.width > 0 && map.height > 0) move() else map.post { if (map.width > 0) move() else g.moveCamera(CameraUpdateFactory.newLatLngZoom(LatLng(lat, lng), 12f)) }
    }

    private fun placeMarkers(g: GoogleMap) {
        for (m in ids.keys) m.remove()
        ids.clear()
        for (m in markers) {
            val opts = MarkerOptions().position(LatLng(m.at.lat, m.at.lng))
            if (m.title.isNotEmpty()) opts.title(m.title)
            if (m.subtitle.isNotEmpty()) opts.snippet(m.subtitle)
            m.color?.let { v ->
                // Its hue.
                val hsv = FloatArray(3)
                Color.colorToHSV(AsukaModules.color(v), hsv)
                opts.icon(BitmapDescriptorFactory.defaultMarker(hsv[0]))
            }
            g.addMarker(opts)?.let { ids[it] = m.id }
        }
    }
}

fun makeView(events: ViewEvents): View = MapsView(events)
