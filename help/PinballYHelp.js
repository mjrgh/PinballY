var isTOC = false;

var contents = [
   "OptionSettings.html Option Settings",
        "+AttractModeOptions.html Attract Mode",
        "+ButtonOptions.html Buttons/Keys",
        "+CaptureOptions.html Media Capture",
        "+CoinOptions.html Coins",
        "+GameLaunchOptions.html Game Launch Options",
        "+GameWheelOptions.html Game Wheel Options",
        "+InfoBoxOptions.html Info Box Options",
        "+InstCardOptions.html Instruction Card Options",
        "+LogFileOptions.html Log File Options",
        "+MenuOptions.html Menu Options",
        "+PathOptions.html Folder Paths",
        "+RealDMDOptions.html Real DMD Setup",
        "+StartupOptions.html Startup Options",
        "+StatuslineOptions.html Statusline Options",
        "+SystemOptions.html Pinball Player System Setup",
            "++SystemOptionsBrowseSubfolder.html System Setup - Browse Subfolders",
    "Javascript.html Javascript scripting",
        "+MainWindowObject.html mainWindow Object",
        "+SystemInfoObject.html systemInfo Object",
        "+DllImport.html Calling Native DLLs from Javascript",
    "AdminMode.html Administrator Mode",
    "DirectoryInfo.html Files &amp; Folders",
    "DOFEvents.html DOF Events",
    "Credits.html Credits and disclaimers",
    "../VersionHistory.txt Version history",
];

$(function()
{
    /([^/]+)$/.test(window.location.pathname);
    var curFile = RegExp.$1;

    $("body").wrapInner($("<div id=\"mainContent\"></div>"));
    $("#mainContent").wrap($("<div id=\"main\"></div>"));

    var root = {
        title: "PinballY Help",
        file: "PinballY.html",
        href: "<a href=\"PinballY.html\">PinballY Help</a>",
        children: []
    };
    var parent = root;
    var level = 0;
    var prv;
    var curItem = root;
    for (var i = 0; i < contents.length; ++i)
    {
        /(\+*)([^\s]+)\s(.*)/.test(contents[i]);
        var itemLevel = RegExp.$1.length;
        var file = RegExp.$2;
        var title = RegExp.$3;

        if (itemLevel > level)
        {
            parent = parent.children[parent.children.length - 1];
            ++level;
        }
        while (itemLevel < level)
        {
            parent = parent.parent;
            --level;
        }

        var target = "";
        if (/\.txt$/.test(file))
            target = " target=\"_blank\"";

        var href = "<a href=\"" + file + "\" " + target + ">" + title + "</a>";
        var hreff = (function(file, target) {
            return function(title) { return "<a href=\"" + file + "\" " + target + ">" + title + "</a>"; };
        })(file, target);

        var item = {
            file: file,
            href: href,
            hreff: hreff,
            title: title,
            level: level,
            parent: parent,
            children: []
        };
        parent.children.push(item);

        item.previous = prv;
        if (prv) prv.next = item;
        prv = item;

        if (file == curFile)
            curItem = item;
    }
    contents = [root];

    var navbar = [];
    function buildNavbar(item)
    {
        if (item.parent)
            buildNavbar(item.parent);

        if (item == curItem)
            navbar.push(item.title);
        else
            navbar.push(item.href);
    }
    buildNavbar(curItem);

    var right = [];
    if (curItem.previous)
        right.push(curItem.previous.hreff("&lt;&lt; " + curItem.previous.title));
    if (curItem.previous && curItem.next)
        right.push("<span style=\"padding: 0px 1em;\"> | </span>");
    if (curItem.next)
        right.push(curItem.next.hreff(curItem.next.title + " &gt;&gt;"));

    navbar = "<div>" + navbar.join(" &gt; ") + "<div class=\"nextPrv\">" + right.join("") + "</div></div>";
    $("body").prepend($("<div class=\"topnavbar\"></div>").append($(navbar)));
    $("body").append($("<div class=\"botnavbar\"></div>").append($(navbar)));

    var tocEle = $("#TOC");
    if (tocEle.length > 0)
    {
        var toc = ["<ul class=\"toc\">"];
        var traverse = function(list)
        {
            for (var i = 0; i < list.length; ++i)
            {
                var item = list[i];
                var target = (/\.txt$/.test(item.file)) ? " target=\"_blank\"" : "";
                toc.push("<li>" + item.href + "</a>");

                if (item.children.length > 0)
                {
                    toc.push("<ul>");
                    traverse(item.children);
                    toc.push("</ul>");
                }
            }
        };
        traverse(curItem.children);
        toc.push("</ul>");
        tocEle.html(toc.join(""));
    }

    var leftBar = ["<div class=\"leftnav\">"];

    var levels;
    var parent = curItem.parent;
    if (!parent || parent == root)
    {
        levels = ["leftnav0", "leftnav1"];
    }
    else
    {
        levels = ["leftnav1", "leftnav2"];
        leftBar.push("<div class=\"leftnav0\">" + parent.href + "</div>");
    }

    var children = curItem == root ? root.children : parent.children;
    for (var i = 0; i < children.length; ++i)
    {
        var c = children[i];
        if (c == curItem)
        {
            leftBar.push("<div class=\"" + levels[0] + " leftnavCur\">" + c.title + "</a></div>");
            for (var j = 0; j < c.children.length ; ++j)
            {
                var cc = c.children[j];
                leftBar.push("<div class=\"" + levels[1] + "\">" + cc.href + "</div>");
            }
        }
        else
            leftBar.push("<div class=\"" + levels[0] + "\">" + c.href + "</div>");
    }

    leftBar.push("</div>");
    $(".topnavbar").append($(leftBar.join("")));
});
