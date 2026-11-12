window.onload = function () {
  window.ui = SwaggerUIBundle({
    url: "/openapi.json",
    dom_id: "#swagger-ui",
    deepLinking: true,
    docExpansion: "list",
    persistAuthorization: false,
    queryConfigEnabled: false,
    validatorUrl: null,
    presets: [SwaggerUIBundle.presets.apis],
    layout: "BaseLayout"
  });
};
